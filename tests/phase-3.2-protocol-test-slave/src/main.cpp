#include <Arduino.h>
#include <hardware/clocks.h>
#include <pico/mutex.h>
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
 *
 * ---------------------------------------------------------------------------
 * WHY TWO CORES
 * ---------------------------------------------------------------------------
 * Rendering one frame costs ~11.6 ms (phase 1.1). A CLOCK_UPDATE frame is 295 B
 * on the wire, which at 1 Mbaud arrives in ~3 ms. So while one frame is being
 * drawn, three or four more land on the UART -- and receiving them in the same
 * loop that draws means they are simply not read while drawing is in progress.
 *
 * So the work is split across the RP2040's two cores:
 *
 *   core 0  owns Serial1 (RS485) and Serial (USB). It does nothing but read
 *           bytes, parse frames, and print the once-a-second report. It never
 *           blocks on the display.
 *   core 1  owns the display and nothing else. Every LovyanGFX call lives here.
 *
 * They meet at one mutex-guarded mailbox (below), which is deliberately a
 * single slot and not a queue: see the comment there.
 *
 * All printing happens on core 0. SerialUSB guards each individual call with a
 * mutex, so two cores printing at once cannot corrupt anything -- but their
 * output interleaves mid-line into unreadable garbage, so core 1 stays quiet
 * and publishes counters for core 0 to report instead.
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

/*
 * ---------------------------------------------------------
 * STARTUP HANDSHAKE
 * ---------------------------------------------------------
 * The arduino-pico core launches core 1 BEFORE core 0 enters setup(), so
 * setup1() can reach display.init() before core 0 has repointed the peripheral
 * clock with clock_configure() and the SPI bus would come up clocked from
 * the 48 MHz USB clock instead of the 133 MHz system clock. Core 1 waits on
 * until this flags is set to true, which happens at the end of setup() on core 0.
 */
static volatile bool coreZeroReady = false;

/*
 * ---------------------------------------------------------
 * THE CROSS-CORE MAILBOX
 * ---------------------------------------------------------
 * Core 0 writes, core 1 reads. Always latest-wins: core 0 overwrites whatever
 * core 1 has not picked up yet, so a render that runs long can never build up a
 * backlog of stale angles to work through. It just skips to the newest ones.
 * A queue would do the opposite and turn a slow frame into permanent lag.
 *
 * The critical section copies ~60 bytes and is over in well under a
 * microsecond, so a plain blocking mutex is the right tool here; a lock-free
 * scheme would be more code for nothing.
 */
struct Mailbox {
  ClockNodeData  angles;                    // from CMD_CLOCK_UPDATE, this node's slot only
  ClockInitData  clockInit;                 // from CMD_CLOCK_INIT
  WidgetInitData widgetInit;                // from CMD_WIDGET_INIT
  uint8_t        widgetField;               // from CMD_WIDGET_UPDATE
  uint8_t        widgetData[MAX_WIDGET_DATA];
  uint8_t        widgetDataLen;

  // One "unread" flag per message kind, not one combined flag: core 1 needs to
  // know WHICH of the four things changed, since each triggers a different,
  // independent chunk of render logic (reconfigure hands, switch to widget
  // mode, redraw text, redraw hands). A single flag couldn't tell those apart.
  //
  // Lifecycle: core 0 sets a flag to true after writing new data for that kind
  // (deliverFrame()). Core 1 copies the whole struct out, then clears all four
  // flags back to false in the same locked section (loop1()),. So its local
  // copy still remembers what was new, while `shared` goes back to "nothing
  // pending" for the next frame.
  bool anglesDirty;
  bool clockInitDirty;
  bool widgetInitDirty;
  bool widgetDirty;
};

static Mailbox shared;
static mutex_t sharedMutex;

/*
 * ---------------------------------------------------------
 * COUNTERS
 * ---------------------------------------------------------
 * Core 1 publishes, core 0 reports. These are plain volatile uint32_t rather
 * than mutex-guarded: a 32-bit aligned load or store is atomic on the
 * Cortex-M0+, and the first two are monotonic, so core 0 taking deltas is
 * correct even across their wrap.
 *
 * renderMicrosMax is the one exception. Core 0 reads it and zeroes it to
 * start a new window. If core 1 happens to write a new maximum in between, that
 * one sample is lost from the report. Losing one sample per second is not worth
 * a lock on the render loop's hot path.
 */
static volatile uint32_t renderedFrames  = 0;   // monotonic
static volatile uint32_t renderMicrosSum = 0;   // monotonic, wraps harmlessly
static volatile uint32_t renderMicrosMax = 0;   // read-and-zeroed by core 0

// Core 0 private -- receive-side statistics for the once-a-second report.
static uint32_t framesOk = 0;
static uint32_t framesBad = 0;
static uint32_t seqGaps = 0;
static uint32_t superseded = 0;
static uint8_t  lastSequence = 0;
static bool     haveLastSequence = false;
static ParseResult lastError = PARSE_OK;

/*
 * ===========================================================================
 * CORE 0 -- RS485 RECEIVE
 * ===========================================================================
 */

// Receive buffers. rxBuf collects the COBS-encoded bytes between two
// delimiters; scratch is where parseFrame() decodes them. Frame::payload points
// into scratch, so scratch must stay alive for as long as the frame is in use.
static uint8_t rxBuf[MAX_ENCODED_FRAME];
static uint8_t scratch[MAX_DECODED_FRAME];
static size_t  rxLen = 0;
static bool    rxOverflow = false;

// Hands one parsed frame to core 1. This is the whole of the slave's "logic":
// copy the bytes that matter into the mailbox and get straight back to reading
// the bus. Nothing is interpreted here beyond picking this node's slot.
static void deliverFrame(const Frame& f) {
  mutex_enter_blocking(&sharedMutex); //block the other core from accessing the mailbox

  switch (f.header.command) {
    case CMD_CLOCK_INIT:
      // parseFrame() already proved the payload is exactly sizeof(ClockInitData),
      // so this copy cannot read off the end of the buffer.
      memcpy(&shared.clockInit, f.payload, sizeof(ClockInitData));
      shared.clockInitDirty = true;
      break;

    case CMD_WIDGET_INIT:
      memcpy(&shared.widgetInit, f.payload, sizeof(WidgetInitData));
      shared.widgetInitDirty = true;
      break;

    case CMD_CLOCK_UPDATE: {
      const GlobalClockUpdate* update = (const GlobalClockUpdate*)f.payload;

      // Core 1 never got to the previous one. That is the number which says
      // the node has fallen behind the bus, as opposed to the bus dropping it.
      if (shared.anglesDirty) superseded++;

      shared.angles = update->nodes[MY_NODE_ID];
      shared.anglesDirty = true;
      break;
    }

    case CMD_WIDGET_UPDATE: {
      // Only the used prefix was transmitted, so the real data length is
      // whatever is left after the fieldId byte.
      const WidgetUpdateData* widget = (const WidgetUpdateData*)f.payload;
      size_t dataLen = f.payloadLen - 1;
      if (dataLen > MAX_WIDGET_DATA) dataLen = MAX_WIDGET_DATA;   // parseFrame already caps this

      shared.widgetField = widget->fieldId;
      memcpy(shared.widgetData, widget->data, dataLen);
      shared.widgetDataLen = (uint8_t)dataLen;
      shared.widgetDirty = true;
      break;
    }
  }

  mutex_exit(&sharedMutex); //deblock the mailbox for the other core to access
}

static void acceptFrame(const Frame& f) {
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

  deliverFrame(f);
}

void setup() {
  // bind the peripheral clock to the system clock (133 MHz)
  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys),
                  clock_get_hz(clk_sys));

  mutex_init(&sharedMutex);

  // Release core 1 now, before the USB wait below -- the display should come up
  // immediately, not two seconds late because nobody opened a serial monitor.
  // The barrier makes sure the clock and mutex setup above is visible to core 1
  // before it sees the flag.
  __sync_synchronize();
  coreZeroReady = true;

  // 1. Start USB Serial for debugging messages on your computer screen
  Serial.begin(115200);
  while (!Serial && millis() < 2000); // Wait up to 2 seconds for USB serial to connect
  Serial.print("RS485 Slave Initializing as node ");
  Serial.println(MY_NODE_ID);

  // 2. Configure the Direction Control Pin for permanent listening
  pinMode(RS485_CTRL_PIN, OUTPUT);
  digitalWrite(RS485_CTRL_PIN, LOW); // Lock into Receive mode

  // 3. Setup Hardware Serial (UART0) pins and baud rate.
  //
  // The FIFO must be sized BEFORE begin(); setFIFOSize() is ignored once the
  // port is running. The default is 32 bytes, which is a tenth of a single
  // CLOCK_UPDATE frame -- any hesitation on this core (a USB print that blocks,
  // say) would drop bytes mid-frame. 1 KB out of the RP2040's 264 KB buys about
  // three frames of slack and removes the whole failure class.
  Serial1.setTX(RS485_TX_PIN);
  Serial1.setRX(RS485_RX_PIN);
  Serial1.setFIFOSize(1024);
  Serial1.begin(RS485_BAUD_RATE);

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

    //if received byte equals the delimiter, parse the frame and reset the buffer for the next frame
    if (inByte == FRAME_DELIMITER) {
      if (rxOverflow) {
        framesBad++;
        lastError = PARSE_ERR_SHORT;   // oversized: never a frame we could hold
      } else if (rxLen > 0) {
        Frame frame;
        ParseResult result = parseFrame(rxBuf, rxLen, scratch, sizeof(scratch), &frame);
        if (result == PARSE_OK) {
          framesOk++;
          acceptFrame(frame);
        } else {
          framesBad++;
          lastError = result;
        }
      }
      rxLen = 0;
      rxOverflow = false;
    } else if (rxLen < sizeof(rxBuf)) {
      rxBuf[rxLen++] = inByte; //buffer not full yet, append received byte
    } else {
      // Too long to be a valid frame. Keep discarding until the next delimiter
      // rather than overwriting the buffer.
      rxOverflow = true;
    }
  }

  // ---- once a second: report link health and what core 1 managed to draw ----
  static unsigned long lastReportTime = millis();
  static uint32_t lastRenderedFrames = 0;
  static uint32_t lastRenderMicros = 0;

  unsigned long now = millis();
  if (now - lastReportTime >= 1000) {
    unsigned long window = now - lastReportTime;
    lastReportTime = now;

    // Unsigned deltas, so these stay correct across a counter wrap.
    uint32_t frames = renderedFrames - lastRenderedFrames;
    uint32_t micrs  = renderMicrosSum - lastRenderMicros;
    lastRenderedFrames += frames;
    lastRenderMicros   += micrs;

    uint32_t peak = renderMicrosMax;
    renderMicrosMax = 0;

    Serial.print("[node ");
    Serial.print(MY_NODE_ID);
    Serial.print("] rx ");
    Serial.print(framesOk * 1000.0f / window, 1);
    Serial.print(" f/s | render ");
    Serial.print(frames * 1000.0f / window, 1);
    Serial.print(" f/s (avg ");
    Serial.print(frames ? (micrs / (float)frames / 1000.0f) : 0.0f, 2);
    Serial.print("ms max ");
    Serial.print(peak / 1000.0f, 2);
    Serial.print("ms) | ");
    Serial.print(framesBad);
    Serial.print(" bad | ");
    Serial.print(seqGaps);
    Serial.print(" gaps | ");
    Serial.print(superseded);
    Serial.print(" superseded | fifo ");
    // overflow() is read-and-clear, so this answers "did the UART overrun at any
    // point since the last report", which is exactly the question worth asking.
    Serial.print(Serial1.overflow() ? "OVERRUN" : "ok");
    if (framesBad > 0) {
      Serial.print(" | last error: ");
      Serial.print(parseResultName(lastError));
    }
    Serial.println();

    framesOk = 0;
    framesBad = 0;
    seqGaps = 0;
    superseded = 0;
  }
}

/*
 * ===========================================================================
 * CORE 1 -- DISPLAY
 * ===========================================================================
 * Owns the display and touches nothing else. What it draws is decided entirely
 * by which init frame arrived last, so the master switches every node's display
 * mode simply by broadcasting CLOCK_INIT or WIDGET_INIT.
 */

enum RenderMode : uint8_t {
  RENDER_WAITING,   // nothing to draw on yet: no init frame has arrived
  RENDER_CLOCK,
  RENDER_WIDGET
};

static RenderMode renderMode = RENDER_WAITING;

// The configuration currently baked into the pre-drawn hand sprites. A
// CLOCK_INIT carrying the same values as this one is a no-op -- which is what
// lets the master re-broadcast the config every few seconds for the benefit of
// a node that rebooted, without every node rebuilding its sprites and flashing
// a full-screen repaint each time.
static ClockInitData appliedClockInit;
static bool          haveClockInit = false;

static uint16_t widgetBgColor = TFT_BLACK;
static char     widgetText[MAX_WIDGET_DATA + 1] = "";

// Widget rendering, kept as dumb as it can be: the field value as one line of
// text in the middle of the face. Only a band across the middle is repainted,
// so an update does not flash the whole screen.
static void drawWidgetText() {
  display.fillRect(0, 96, display.width(), 48, widgetBgColor);
  display.setFont(&fonts::Font4);
  display.setTextColor(TFT_WHITE, widgetBgColor);
  display.setTextDatum(middle_center);
  display.drawString(widgetText, display.width() / 2, display.height() / 2);
}

void setup1() {
  while (!coreZeroReady) delay(1);   // see the startup handshake note above

  display.init();
  display.setRotation(0);
  display.setBrightness(255);
  display.fillScreen(TFT_BLACK);

  // clockFace.begin() is deliberately NOT called here -- it waits for the
  // CMD_CLOCK_INIT broadcast, which is what configures the hands.
}

void loop1() {
  // Take one consistent copy of everything the mailbox holds, then let core 0
  // carry on writing into it while this core spends the next ~10 ms drawing.
  Mailbox snap;
  mutex_enter_blocking(&sharedMutex);
  snap = shared;
  shared.anglesDirty     = false;
  shared.clockInitDirty  = false;
  shared.widgetInitDirty = false;
  shared.widgetDirty     = false;
  mutex_exit(&sharedMutex);

  // Nothing arrived. Without this, an idle core 1 would re-take the mutex as
  // fast as it can run contending with core 0,
  // which needs that same mutex to hand over every frame it receives. 200 us
  // caps this core at ~5 kHz of polling and costs at most 0.2 ms of latency on
  // a frame that only turns up every 16 ms.
  if (!snap.anglesDirty && !snap.clockInitDirty &&
      !snap.widgetInitDirty && !snap.widgetDirty) {
    delayMicroseconds(200);
    return;
  }
 
  // ---- CLOCK_INIT: (re)configure the face and switch to clock mode ----
  if (snap.clockInitDirty) {

    // True on the first CLOCK_INIT ever, or when the config actually changed
    // false for the master's periodic re-broadcast, so that stays a no-op.
    bool changed = !haveClockInit ||
                   memcmp(&appliedClockInit, &snap.clockInit, sizeof(ClockInitData)) != 0;

    if (changed) {
      clockFace.configure(snap.clockInit.hand1Color, snap.clockInit.hand2Color,
                          snap.clockInit.hand1Thickness, snap.clockInit.hand2Thickness,
                          snap.clockInit.hand1LengthPct, snap.clockInit.hand2LengthPct);
      clockFace.begin();          // rebuilds the hand sprites, and invalidates
      appliedClockInit = snap.clockInit;
      haveClockInit = true;
    } else if (renderMode != RENDER_CLOCK) {
      // Same config, but the widget had taken the screen over. The frame buffer
      // is still correct, the glass is not -- so force one full-screen push.
      display.fillScreen(TFT_BLACK);
      clockFace.invalidate();
    }

    renderMode = RENDER_CLOCK;
  }

  // ---- WIDGET_INIT: switch to widget mode ----
  if (snap.widgetInitDirty) {
    widgetBgColor = snap.widgetInit.bgColor;
    renderMode = RENDER_WIDGET;
    display.fillScreen(widgetBgColor);
    drawWidgetText();             // repaint whatever value was last received
  }

  // ---- WIDGET_UPDATE: new field value ----
  if (snap.widgetDirty) {
    size_t n = snap.widgetDataLen;
    if (n > MAX_WIDGET_DATA) n = MAX_WIDGET_DATA;
    memcpy(widgetText, snap.widgetData, n);
    widgetText[n] = '\0';         // the wire carries no terminator

    if (renderMode == RENDER_WIDGET) drawWidgetText();
    // In clock mode the frame is still received, parsed and validated -- it
    // just isn't drawn. That keeps the variable-length parser path under test
    // while the clock is the thing on screen.
  }

  // ---- CLOCK_UPDATE: the hot path ----
  if (snap.anglesDirty && renderMode == RENDER_CLOCK) {
    uint32_t t0 = micros();

    // The master sends tenths of a degree, which is exactly what prepareFrame()
    // wants, so the received values go straight through with no conversion.
    clockFace.prepareFrame(snap.angles.angle1DeciDeg, snap.angles.angle2DeciDeg);
    clockFace.pushFrame();

    uint32_t elapsed = micros() - t0;
    renderedFrames  = renderedFrames + 1;
    renderMicrosSum = renderMicrosSum + elapsed;
    if (elapsed > renderMicrosMax) renderMicrosMax = elapsed;
  }
}
