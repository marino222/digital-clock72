#include <Arduino.h>
#include "pin_definitions.h"
#include "protocol.h"

/*
 * Phase 3.2 -- protocol master.
 *
 * Broadcasts the real thing, not a reduced stand-in: a full 288-byte
 * GlobalClockUpdate covering all 72 nodes, 60 times per second, exactly as the
 * finished clock would. A smaller test packet would sail over the bus and prove
 * nothing about whether the design actually fits in the RS485 budget.
 *
 * Budget check, against the ~100 kB/s ceiling measured in phase 2.1:
 *   288 B payload -> 295 B on the wire after COBS
 *   295 B x 60 Hz = 17.7 kB/s   (about 18% of the bus)
 *
 * CLOCK_INIT and WIDGET_UPDATE are mixed in at a low rate so those paths get
 * exercised on real hardware too, rather than only the one hot path.
 */

const unsigned long SEND_INTERVAL_MS   = 16;    // ~60 Hz clock updates
const unsigned long INIT_INTERVAL_MS   = 5000;  // re-broadcast config every 5 s
const unsigned long WIDGET_INTERVAL_MS = 2000;  // exercise the widget path
const unsigned long REPORT_INTERVAL_MS = 1000;  // throughput report over USB

// Hand rotation speeds, matching the phase-1.1 display test (20 and 100 deg/s).
//
// The angles are accumulated in MILLIdegrees, not whole degrees. At 60 Hz the
// slower hand only moves ~0.33 deg per packet, so a whole-degree accumulator
// would truncate every step to 0 and the hand would never move. Millidegrees
// give the accumulator enough headroom to keep integer math exact, and the
// value is divided down to tenths of a degree only when the packet is filled.
//
// Handy coincidence: millidegrees-per-millisecond is the same number as
// degrees-per-second, so these constants read as their real-world speed.
const uint32_t HAND1_MDEG_PER_MS = 20;    // deg/s
const uint32_t HAND2_MDEG_PER_MS = 100;   // deg/s
const uint32_t FULL_TURN_MDEG = 360000;   // one revolution

// Each node is given a fixed slice of a revolution as its starting offset, so
// no two displays sit at the same angle. That is what makes it obvious on the
// bench that every slave is reading ITS OWN slot of the broadcast rather than
// slot 0 -- if the addressing were broken, all displays would move in unison.
const uint32_t NODE_PHASE_MDEG = FULL_TURN_MDEG / TOTAL_MATRIX_NODES;

// Kept off the stack: together these are ~600 bytes, and buildFrame() needs
// another 292 for its own assembly buffer.
static GlobalClockUpdate txUpdate;
static uint8_t txBuf[MAX_ENCODED_FRAME];

static uint8_t sequenceCounter = 0;

// Bytes actually put on the wire since the last report, for the budget check.
static uint32_t bytesThisSecond = 0;
static uint32_t framesThisSecond = 0;

// Sends one frame and accounts for it. Returns false if the frame could not be
// built, which would mean a programming error rather than a bus problem.
static bool sendFrame(CommandType cmd, uint8_t targetNode,
                      const void* payload, size_t payloadLen) {
  size_t n = buildFrame(cmd, targetNode, sequenceCounter++, payload, payloadLen,
                        txBuf, sizeof(txBuf));
  if (n == 0) {
    Serial.println("[ERROR] buildFrame failed -- payload too large for the buffer?");
    return false;
  }

  Serial1.write(txBuf, n);
  Serial1.flush();          // let the UART drain before the next frame is queued

  bytesThisSecond += n;
  framesThisSecond++;
  return true;
}

// Pushes the clock-face configuration every node needs before it can render.
// Re-sent periodically so a slave that reboots mid-test picks it up again
// without the master having to be restarted.
static void broadcastClockInit() {
  ClockInitData init;
  init.hand1Color     = 0xF800;  // RGB565 red
  init.hand2Color     = 0xFFFF;  // RGB565 white
  init.bgColor        = 0x0000;  // black
  init.hand1LengthPct = 60;
  init.hand2LengthPct = 90;
  init.hand1Thickness = 8;
  init.hand2Thickness = 4;

  sendFrame(CMD_CLOCK_INIT, GLOBAL_BROADCAST_ID, &init, sizeof(init));
}

// Widget updates are node-targeted and carry only the bytes they actually use,
// so this frame is 12 bytes on the wire rather than the full 33 the struct
// could hold.
static void sendWidgetUpdate(uint8_t targetNode) {
  WidgetUpdateData widget;
  widget.fieldId = 1;

  const char* text = "hello";
  const size_t textLen = strlen(text);
  memcpy(widget.data, text, textLen);

  sendFrame(CMD_WIDGET_UPDATE, targetNode, &widget, 1 + textLen);
}

void setup() {
  // bind the peripheral clock to the system clock (133 MHz)
  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys),
                  clock_get_hz(clk_sys));

  // 1. Start USB Serial for communication with PC
  Serial.begin(115200);
  while (!Serial && millis() < 2000); // Wait up to 2 seconds for USB serial to connect
  Serial.println("RS485 Master Initializing...");

  // 2. Configure the RS485 module for transmit mode
  pinMode(RS485_CTRL_PIN, OUTPUT);
  digitalWrite(RS485_CTRL_PIN, HIGH);

  // 3. Setup Hardware Serial (UART0) pins and baud rate
  Serial1.setTX(RS485_TX_PIN);
  Serial1.setRX(RS485_RX_PIN);
  Serial1.begin(RS485_BAUD_RATE);

  Serial.print("Protocol v");
  Serial.print(PROTOCOL_VERSION);
  Serial.print(" | CLOCK_UPDATE payload ");
  Serial.print((unsigned)sizeof(GlobalClockUpdate));
  Serial.print(" B, max frame ");
  Serial.print((unsigned)MAX_ENCODED_FRAME);
  Serial.println(" B");

  broadcastClockInit();
  Serial.println("Master ready. Broadcasting packets...");
}

void loop() {
  // Initialized on the first call to loop(), i.e. after setup() has already
  // burned up to 2 seconds waiting for USB serial. Starting from 0 instead
  // would make the first packet report that whole wait as elapsed time and
  // jump the hands most of a revolution.
  static unsigned long lastSendTime   = millis();
  static unsigned long lastInitTime   = millis();
  static unsigned long lastWidgetTime = millis();
  static unsigned long lastReportTime = millis();

  // Current hand positions for node 0, in millidegrees (see the constants
  // above). Every other node is this plus its own phase offset.
  static uint32_t angle1Milli = 0, angle2Milli = 0;

  unsigned long now = millis();

  // ---- 60 Hz: the full 72-node clock broadcast ----
  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    // How much time actually passed since the last packet. Using the real
    // elapsed time, rather than assuming exactly SEND_INTERVAL_MS, keeps the
    // hands turning at the right speed even if a loop runs late.
    unsigned long elapsedMs = now - lastSendTime;
    lastSendTime = now;

    // Advance both hands, wrapping at a full revolution.
    angle1Milli = (angle1Milli + HAND1_MDEG_PER_MS * elapsedMs) % FULL_TURN_MDEG;
    angle2Milli = (angle2Milli + HAND2_MDEG_PER_MS * elapsedMs) % FULL_TURN_MDEG;

    // Fill all 72 slots. Tenths of a degree, 0..3599, is the resolution the
    // wire carries and exactly what the slave hands to ClockFace::prepareFrame(),
    // so no conversion happens on the receiving end. Whole degrees weren't
    // enough: at these speeds a hand can move less than 1 degree between
    // packets, which made the motion step unevenly instead of gliding.
    for (uint8_t node = 0; node < TOTAL_MATRIX_NODES; node++) {
      uint32_t offset = uint32_t(node) * NODE_PHASE_MDEG;
      txUpdate.nodes[node].angle1DeciDeg =
          uint16_t(((angle1Milli + offset) % FULL_TURN_MDEG) / 100);
      txUpdate.nodes[node].angle2DeciDeg =
          uint16_t(((angle2Milli + offset) % FULL_TURN_MDEG) / 100);
    }

    sendFrame(CMD_CLOCK_UPDATE, GLOBAL_BROADCAST_ID, &txUpdate, sizeof(txUpdate));
  }

  // ---- every 5 s: re-broadcast the clock face configuration ----
  if (now - lastInitTime >= INIT_INTERVAL_MS) {
    lastInitTime = now;
    broadcastClockInit();
  }

  // ---- every 2 s: exercise the node-targeted widget path ----
  if (now - lastWidgetTime >= WIDGET_INTERVAL_MS) {
    lastWidgetTime = now;
    sendWidgetUpdate(0);
  }

  // ---- once a second: report what the bus is actually carrying ----
  if (now - lastReportTime >= REPORT_INTERVAL_MS) {
    unsigned long window = now - lastReportTime;
    lastReportTime = now;

    Serial.print("TX ");
    Serial.print(framesThisSecond * 1000.0f / window, 1);
    Serial.print(" frames/s | ");
    Serial.print(bytesThisSecond * 1000.0f / window / 1000.0f, 1);
    Serial.print(" kB/s of ~100 kB/s | seq ");
    Serial.println(sequenceCounter);

    bytesThisSecond = 0;
    framesThisSecond = 0;
  }
}
