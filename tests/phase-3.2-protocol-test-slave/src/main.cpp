#include <Arduino.h>
#include <hardware/clocks.h>
#include "protocol.h"
#include "pin_definitions.h"
#include "LGFX_Config.hpp"
#include "ClockFace.hpp"

/*
 * Phase 3.2 -- protocol slave.
 *
 * Listens to the master's broadcast, picks its own slot out of the 72-node
 * CLOCK_UPDATE payload, and renders it.
 *
 * Addressing is manual in this phase (daisy-chain auto-addressing comes later),
 * so the node number is baked in at build time -- see the slave0/slave1
 * environments in platformio.ini.
 */
#ifndef MY_NODE_ID
#define MY_NODE_ID 0
#endif

static_assert(MY_NODE_ID < TOTAL_MATRIX_NODES, "MY_NODE_ID must be 0..71");

LGFX display;

const int16_t RADIUS = 110;

// Constructed with placeholder values; the real ones arrive in a CMD_CLOCK_INIT
// frame and are applied via configure() before begin() runs.
ClockFace clockFace(display, RADIUS, TFT_RED, TFT_WHITE, 8, 4);
static bool clockReady = false;

// Receive buffers. rxBuf collects the COBS-encoded bytes between two
// delimiters; scratch is where parseFrame() decodes them. Frame::payload points
// into scratch, so scratch must stay alive for as long as the frame is in use.
static uint8_t rxBuf[MAX_ENCODED_FRAME];
static uint8_t scratch[MAX_DECODED_FRAME];
static size_t  rxLen = 0;
static bool    rxOverflow = false;

// Statistics for the once-a-second report.
static uint32_t framesOk = 0;
static uint32_t framesBad = 0;
static uint32_t seqGaps = 0;
static uint8_t  lastSequence = 0;
static bool     haveLastSequence = false;
static ParseResult lastError = PARSE_OK;

static void handleClockInit(const Frame& f) {
  // parseFrame() already proved the payload is exactly sizeof(ClockInitData),
  // so this cast cannot read off the end of the buffer.
  const ClockInitData* init = (const ClockInitData*)f.payload;

  if (clockReady) return;   // config is re-broadcast periodically; apply it once

  clockFace.configure(init->hand1Color, init->hand2Color,
                      init->hand1Thickness, init->hand2Thickness,
                      init->hand1LengthPct, init->hand2LengthPct);
  clockFace.begin();
  clockReady = true;

  Serial.print("[INIT] hands ");
  Serial.print(init->hand1LengthPct);
  Serial.print("%/");
  Serial.print(init->hand2LengthPct);
  Serial.print("% thickness ");
  Serial.print(init->hand1Thickness);
  Serial.print("/");
  Serial.print(init->hand2Thickness);
  Serial.println(" -- clock face ready");
}

static void handleClockUpdate(const Frame& f) {
  if (!clockReady) return;  // nothing to draw on until CLOCK_INIT has arrived

  const GlobalClockUpdate* update = (const GlobalClockUpdate*)f.payload;
  const ClockNodeData& mine = update->nodes[MY_NODE_ID];

  // The master sends tenths of a degree, which is exactly what prepareFrame()
  // wants, so the received values go straight through with no conversion.
  clockFace.prepareFrame(mine.angle1DeciDeg, mine.angle2DeciDeg);
  clockFace.pushFrame();
}

static void handleWidgetUpdate(const Frame& f) {
  // Only the used prefix was transmitted, so the real data length is whatever
  // is left after the fieldId byte.
  const WidgetUpdateData* widget = (const WidgetUpdateData*)f.payload;
  const size_t dataLen = f.payloadLen - 1;

  Serial.print("[WIDGET] field ");
  Serial.print(widget->fieldId);
  Serial.print(" | ");
  Serial.print((unsigned)dataLen);
  Serial.print(" B | ");
  for (size_t i = 0; i < dataLen; i++) {
    Serial.write(isPrintable(widget->data[i]) ? (char)widget->data[i] : '.');
  }
  Serial.println();
}

static void applyFrame(const Frame& f) {
  // Ignore anything addressed to a different node. CLOCK_UPDATE is always a
  // broadcast, so it never gets filtered out here.
  if (f.header.targetNode != GLOBAL_BROADCAST_ID &&
      f.header.targetNode != MY_NODE_ID) {
    return;
  }

  // A gap in the rolling counter means frames were dropped on the bus. Widget
  // and init frames share the counter, so this tracks the whole stream.
  if (haveLastSequence) {
    uint8_t expected = lastSequence + 1;   // deliberately wraps at 255
    if (f.header.sequence != expected) seqGaps++;
  }
  lastSequence = f.header.sequence;
  haveLastSequence = true;

  switch (f.header.command) {
    case CMD_CLOCK_INIT:    handleClockInit(f);    break;
    case CMD_CLOCK_UPDATE:  handleClockUpdate(f);  break;
    case CMD_WIDGET_UPDATE: handleWidgetUpdate(f); break;
    case CMD_WIDGET_INIT:   /* nothing renders widgets yet */ break;
  }
}

void setup() {
  // bind the peripheral clock to the system clock (133 MHz)
  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys),
                  clock_get_hz(clk_sys));

  // 1. Start USB Serial for debugging messages on your computer screen
  Serial.begin(115200);
  while (!Serial && millis() < 2000); // Wait up to 2 seconds for USB serial to connect
  Serial.print("RS485 Slave Initializing as node ");
  Serial.println(MY_NODE_ID);

  // 2. Configure the Direction Control Pin for permanent listening
  pinMode(RS485_CTRL_PIN, OUTPUT);
  digitalWrite(RS485_CTRL_PIN, LOW); // Lock into Receive mode

  // 3. Setup Hardware Serial (UART0) pins and baud rate
  Serial1.setTX(RS485_TX_PIN);
  Serial1.setRX(RS485_RX_PIN);
  Serial1.begin(RS485_BAUD_RATE);

  display.init();
  display.setRotation(0);
  display.setBrightness(255);

  // clockFace.begin() is deliberately NOT called here -- it waits for the
  // CMD_CLOCK_INIT broadcast, which is what configures the hands.
  Serial.println("Slave ready. Waiting for CLOCK_INIT...");
}

void loop() {
  /*
   * COBS framing makes this loop much simpler than the three-state sync-byte
   * machine phase 2.1 needed. 0x00 cannot occur inside an encoded frame, so a
   * single 0x00 unambiguously ends one frame and starts the next -- and a
   * receiver that gets lost mid-stream resynchronises automatically at the very
   * next delimiter, with no state to unwind.
   */
  while (Serial1.available() > 0) {
    uint8_t inByte = Serial1.read();

    if (inByte == FRAME_DELIMITER) {
      if (rxOverflow) {
        framesBad++;
        lastError = PARSE_ERR_SHORT;   // oversized: never a frame we could hold
      } else if (rxLen > 0) {
        Frame frame;
        ParseResult result = parseFrame(rxBuf, rxLen, scratch, sizeof(scratch), &frame);
        if (result == PARSE_OK) {
          framesOk++;
          applyFrame(frame);
        } else {
          framesBad++;
          lastError = result;
        }
      }
      rxLen = 0;
      rxOverflow = false;
    } else if (rxLen < sizeof(rxBuf)) {
      rxBuf[rxLen++] = inByte;
    } else {
      // Too long to be a valid frame. Keep discarding until the next delimiter
      // rather than overwriting the buffer.
      rxOverflow = true;
    }
  }

  // ---- once a second: report link health ----
  static unsigned long lastReportTime = millis();
  unsigned long now = millis();
  if (now - lastReportTime >= 1000) {
    unsigned long window = now - lastReportTime;
    lastReportTime = now;

    Serial.print("[node ");
    Serial.print(MY_NODE_ID);
    Serial.print("] ");
    Serial.print(framesOk * 1000.0f / window, 1);
    Serial.print(" frames/s ok | ");
    Serial.print(framesBad);
    Serial.print(" bad | ");
    Serial.print(seqGaps);
    Serial.print(" seq gaps");
    if (framesBad > 0) {
      Serial.print(" | last error: ");
      Serial.print(parseResultName(lastError));
    }
    Serial.println();

    framesOk = 0;
    framesBad = 0;
    seqGaps = 0;
  }
}
