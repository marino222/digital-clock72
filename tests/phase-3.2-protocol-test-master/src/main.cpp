#include <Arduino.h>
#include <hardware/clocks.h>
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
 * All four command types are exercised. Which ones are on the bus right now is
 * controlled by single-key commands over USB serial (press '?' for the list),
 * but the default mode cycles through everything on its own -- so the master
 * still tests the whole protocol when it is running headless off a USB brick
 * with no terminal attached.
 */

/*
 * ---------------------------------------------------------
 * WHAT IS ON THE BUS
 * ---------------------------------------------------------
 * A "phase" is the traffic pattern currently being transmitted. A "mode" is
 * what the operator asked for: the two pinned modes hold one phase forever,
 * MODE_ALL rotates between them on a timer.
 */
enum Phase : uint8_t {
  PHASE_CLOCK,    // CLOCK_INIT once, then CLOCK_UPDATE at the current rate
  PHASE_WIDGET,   // WIDGET_INIT once, then WIDGET_UPDATE, node-targeted
  PHASE_IDLE      // silence -- the slaves should drop to 0 fps and freeze
};

enum Mode : uint8_t {
  MODE_ALL,       // rotate CLOCK <-> WIDGET, so every command type gets sent
  MODE_CLOCK,     // pinned to PHASE_CLOCK: the clean smoothness run
  MODE_WIDGET,    // pinned to PHASE_WIDGET
  MODE_STOP       // pinned to PHASE_IDLE
};

static Mode  mode  = MODE_ALL;
static Phase phase = PHASE_CLOCK;

// How long MODE_ALL spends in each phase before rotating. The clock phase is
// the longer one: it is the one whose frame rate is actually worth measuring.
const unsigned long CLOCK_PHASE_MS  = 8000;
const unsigned long WIDGET_PHASE_MS = 4000;

// The CLOCK_UPDATE rate is steppable at runtime with '+' / '-'. 60 Hz is the
// design target; 90 and 120 exist to find out what gives out first, the bus or
// the node's render loop.
const uint16_t RATE_TABLE[] = { 30, 60, 90, 120 };
const uint8_t  RATE_COUNT   = sizeof(RATE_TABLE) / sizeof(RATE_TABLE[0]);
static uint8_t rateIndex    = 1;   // -> 60 Hz

// Microseconds, not milliseconds. A 16 ms millis() tick is really 62.5 Hz and
// cannot be stepped finely enough to reach a clean 90 or 120.
static uint32_t sendIntervalUs = 1000000UL / 60;

const unsigned long INIT_INTERVAL_MS        = 5000;  // re-broadcast config during the clock phase
const unsigned long WIDGET_INTERVAL_ALL_MS  = 2000;  // widget traffic mixed into the clock phase
const unsigned long WIDGET_INTERVAL_MS      = 500;   // 2 Hz while the widget phase owns the bus
const unsigned long REPORT_INTERVAL_MS      = 1000;  // throughput report over USB

// The node IDs the two bench slaves are flashed with -- see MY_NODE_ID in the
// slave's platformio.ini. Only WIDGET_UPDATE is node-targeted; everything else
// is a broadcast, so this list exists purely so widget frames alternate between
// the two boards that are actually on the bench.
const uint8_t SLAVE_NODES[] = { 0, 36 };
const uint8_t SLAVE_COUNT   = sizeof(SLAVE_NODES) / sizeof(SLAVE_NODES[0]);

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

/*
 * ---------------------------------------------------------
 * INIT PAYLOADS
 * ---------------------------------------------------------
 * Both init commands cycle through a small table of presets. That is what makes
 * an init frame VISIBLE rather than merely logged: press 'i' and the hands
 * change colour on the spot. The periodic re-broadcast does not advance the
 * table, so a slave that reboots mid-test picks the config back up without the
 * face changing under an operator who did not ask for it.
 */
static const ClockInitData CLOCK_PRESETS[] = {
  // hand1  hand2   bg      len1 len2 thk1 thk2
  { 0xF800, 0xFFFF, 0x0000, 60,  90,  8,   4 },   // red / white
  { 0x07E0, 0xFFE0, 0x0000, 75,  95,  6,   3 },   // green / yellow, longer
  { 0x001F, 0xF81F, 0x0000, 50,  80, 10,   5 }    // blue / magenta, stubbier
};
const uint8_t CLOCK_PRESET_COUNT = sizeof(CLOCK_PRESETS) / sizeof(CLOCK_PRESETS[0]);
static uint8_t clockPreset = 0;

static const uint16_t WIDGET_BG_PRESETS[] = { 0x001F, 0x03E0, 0x7800 };  // navy, dark green, maroon
const uint8_t WIDGET_PRESET_COUNT = sizeof(WIDGET_BG_PRESETS) / sizeof(WIDGET_BG_PRESETS[0]);
static uint8_t widgetPreset = 0;

// Pushes the clock-face configuration every node needs before it can render.
static void broadcastClockInit() {
  sendFrame(CMD_CLOCK_INIT, GLOBAL_BROADCAST_ID,
            &CLOCK_PRESETS[clockPreset], sizeof(ClockInitData));
}

static void broadcastWidgetInit() {
  WidgetInitData init;
  init.widgetType  = 1;
  init.bgColor     = WIDGET_BG_PRESETS[widgetPreset];
  init.fieldCount  = 1;
  init.reserved[0] = 0;
  init.reserved[1] = 0;

  sendFrame(CMD_WIDGET_INIT, GLOBAL_BROADCAST_ID, &init, sizeof(init));
}

// Widget updates are node-targeted and carry only the bytes they actually use,
// so this frame is ~14 bytes on the wire rather than the full 33 the struct
// could hold. The text names the node it was addressed to, so a targeting bug
// shows up on the glass: both displays would read the same label.
static uint32_t widgetCounter = 0;

static void sendWidgetUpdate(uint8_t targetNode) {
  WidgetUpdateData widget;
  widget.fieldId = 1;

  char text[MAX_WIDGET_DATA];
  int len = snprintf(text, sizeof(text), "N%u #%lu",
                     (unsigned)targetNode, (unsigned long)widgetCounter);
  if (len < 0) return;
  if (len > int(sizeof(text)) - 1) len = int(sizeof(text)) - 1;   // snprintf truncated

  memcpy(widget.data, text, (size_t)len);
  sendFrame(CMD_WIDGET_UPDATE, targetNode, &widget, 1 + (size_t)len);
}

/*
 * ---------------------------------------------------------
 * PHASE / MODE CONTROL
 * ---------------------------------------------------------
 */
static const char* phaseName(Phase p) {
  switch (p) {
    case PHASE_CLOCK:  return "CLOCK";
    case PHASE_WIDGET: return "WIDGET";
    case PHASE_IDLE:   return "IDLE";
  }
  return "?";
}

static unsigned long phaseStartedAt = 0;

// When the next CLOCK_UPDATE is due, and the leftover microseconds that have
// not yet added up to a whole millisecond of hand movement. Both live out here
// rather than inside loop() so entering the clock phase can reset them: the
// widget phase does not send clock updates, so without a reset the first packet
// after it would report the entire widget phase as elapsed time and jump the
// hands the better part of a revolution.
static uint32_t lastSendUs = 0;
static uint32_t usCarry    = 0;

// Entering a phase is what sends its init frame -- the slaves switch what they
// render off the last init they saw, so this is also the display mode switch.
static void enterPhase(Phase p) {
  phase = p;
  phaseStartedAt = millis();

  switch (p) {
    case PHASE_CLOCK:
      lastSendUs = micros();
      usCarry    = 0;
      broadcastClockInit();
      break;
    case PHASE_WIDGET: broadcastWidgetInit(); break;
    case PHASE_IDLE:   break;                        // say nothing, send nothing
  }

  Serial.print("[phase] ");
  Serial.println(phaseName(p));
}

static void printHelp() {
  Serial.println();
  Serial.println("  a   ALL     rotate CLOCK <-> WIDGET + widget frames mixed in: all 4 types");
  Serial.println("  c   CLOCK   CLOCK_INIT once, then CLOCK_UPDATE only (clean fps run)");
  Serial.println("  w   WIDGET  WIDGET_INIT once, then WIDGET_UPDATE, alternating nodes");
  Serial.println("  s   STOP    bus goes silent, slaves should fall to 0 fps");
  Serial.println("  i   send one CLOCK_INIT now  (next preset: hands change colour)");
  Serial.println("  I   send one WIDGET_INIT now (next background colour)");
  Serial.println("  u   send one WIDGET_UPDATE now");
  Serial.println("  +/- step the CLOCK_UPDATE rate: 30 / 60 / 90 / 120 Hz");
  Serial.println("  ?   this help");
  Serial.println();
}

static void setMode(Mode m) {
  mode = m;
  switch (m) {
    case MODE_ALL:    Serial.println("[mode] ALL");    enterPhase(PHASE_CLOCK);  break;
    case MODE_CLOCK:  Serial.println("[mode] CLOCK");  enterPhase(PHASE_CLOCK);  break;
    case MODE_WIDGET: Serial.println("[mode] WIDGET"); enterPhase(PHASE_WIDGET); break;
    case MODE_STOP:   Serial.println("[mode] STOP");   enterPhase(PHASE_IDLE);   break;
  }
}

static void stepRate(int8_t dir) {
  int8_t next = int8_t(rateIndex) + dir;
  if (next < 0 || next >= int8_t(RATE_COUNT)) return;   // clamp at the ends

  rateIndex = uint8_t(next);
  sendIntervalUs = 1000000UL / RATE_TABLE[rateIndex];

  Serial.print("[rate] CLOCK_UPDATE at ");
  Serial.print(RATE_TABLE[rateIndex]);
  Serial.println(" Hz");
}

static void handleCommand(char c) {
  switch (c) {
    case 'a': setMode(MODE_ALL);    break;
    case 'c': setMode(MODE_CLOCK);  break;
    case 'w': setMode(MODE_WIDGET); break;
    case 's': setMode(MODE_STOP);   break;

    case 'i':
      clockPreset = (clockPreset + 1) % CLOCK_PRESET_COUNT;
      broadcastClockInit();
      Serial.print("[send] CLOCK_INIT preset ");
      Serial.println(clockPreset);
      break;

    case 'I':
      widgetPreset = (widgetPreset + 1) % WIDGET_PRESET_COUNT;
      broadcastWidgetInit();
      Serial.print("[send] WIDGET_INIT preset ");
      Serial.println(widgetPreset);
      break;

    case 'u':
      sendWidgetUpdate(SLAVE_NODES[widgetCounter % SLAVE_COUNT]);
      widgetCounter++;
      Serial.println("[send] WIDGET_UPDATE");
      break;

    case '+': stepRate(+1); break;
    case '-': stepRate(-1); break;

    case '?': printHelp(); break;

    case '\r':
    case '\n':
    case ' ':
      break;                                    // line noise from the terminal

    default:
      Serial.print("[?] unknown command '");
      Serial.print(c);
      Serial.println("' -- press ? for the list");
      break;
  }
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

  printHelp();
  setMode(MODE_ALL);        // sends the first CLOCK_INIT
  Serial.println("Master ready. Broadcasting packets...");
}

void loop() {
  // Initialized on the first call to loop(), i.e. after setup() has already
  // burned up to 2 seconds waiting for USB serial -- starting these from 0
  // would make every one of them fire immediately on the first pass.
  static unsigned long lastInitTime   = millis();
  static unsigned long lastWidgetTime = millis();
  static unsigned long lastReportTime = millis();

  // Current hand positions for node 0, in millidegrees (see the constants
  // above). Every other node is this plus its own phase offset.
  static uint32_t angle1Milli = 0, angle2Milli = 0;

  unsigned long now = millis();

  // ---- operator commands over USB ----
  while (Serial.available() > 0) {
    handleCommand((char)Serial.read());
  }

  // ---- MODE_ALL: rotate between the two phases on a timer ----
  if (mode == MODE_ALL) {
    unsigned long limit = (phase == PHASE_CLOCK) ? CLOCK_PHASE_MS : WIDGET_PHASE_MS;
    if (now - phaseStartedAt >= limit) {
      enterPhase(phase == PHASE_CLOCK ? PHASE_WIDGET : PHASE_CLOCK);
    }
  }

  // ---- the full 72-node clock broadcast, at the selected rate ----
  // micros() wraps every ~71 minutes; the unsigned subtraction below is
  // correct across that wrap, which a "deadline" comparison would not be.
  uint32_t nowUs = micros();
  if (phase == PHASE_CLOCK && (nowUs - lastSendUs) >= sendIntervalUs) {
    // How much time actually passed since the last packet. Using the real
    // elapsed time, rather than assuming exactly one interval, keeps the hands
    // turning at the right speed even if a loop runs late.
    uint32_t elapsedUs = nowUs - lastSendUs;
    lastSendUs = nowUs;

    // The angle math works in millidegrees per MILLIsecond, so carrying the
    // sub-millisecond remainder is what keeps the hands at exactly the right
    // speed instead of losing a fraction of a step to truncation every packet.
    usCarry += elapsedUs;
    uint32_t elapsedMs = usCarry / 1000;
    usCarry -= elapsedMs * 1000;

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

  // ---- every 5 s during the clock phase: re-broadcast the configuration ----
  // Deliberately does NOT advance the preset, so this is a no-op for a slave
  // that already has the config -- it only matters to one that just rebooted.
  if (now - lastInitTime >= INIT_INTERVAL_MS) {
    lastInitTime = now;
    if (phase == PHASE_CLOCK) broadcastClockInit();
  }

  // ---- the node-targeted widget path ----
  // During the clock phase these still go out, just rarely: the slaves count
  // and validate them without drawing, which keeps the parser's variable-length
  // path under test even while the clock is the thing on screen.
  bool widgetsWanted = (phase == PHASE_WIDGET) ||
                       (phase == PHASE_CLOCK && mode == MODE_ALL);
  unsigned long widgetInterval =
      (phase == PHASE_WIDGET) ? WIDGET_INTERVAL_MS : WIDGET_INTERVAL_ALL_MS;
  if (widgetsWanted && now - lastWidgetTime >= widgetInterval) {
    lastWidgetTime = now;
    sendWidgetUpdate(SLAVE_NODES[widgetCounter % SLAVE_COUNT]);
    widgetCounter++;
  }

  // ---- once a second: report what the bus is actually carrying ----
  if (now - lastReportTime >= REPORT_INTERVAL_MS) {
    unsigned long window = now - lastReportTime;
    lastReportTime = now;

    Serial.print("TX ");
    Serial.print(framesThisSecond * 1000.0f / window, 1);
    Serial.print(" frames/s | ");
    Serial.print(bytesThisSecond * 1000.0f / window / 1000.0f, 1);
    Serial.print(" kB/s of ~100 kB/s | ");
    Serial.print(phaseName(phase));
    Serial.print(" @ ");
    Serial.print(RATE_TABLE[rateIndex]);
    Serial.print(" Hz | seq ");
    Serial.println(sequenceCounter);

    bytesThisSecond = 0;
    framesThisSecond = 0;
  }
}
