// Phase 5.1, section L: power of one node.
//
// Runs steps L1-L9 in order, STEP_MS each, then stays on L9. The step number
// is shown at the bottom of the screen (not readable in L1/L2, backlight off).
//
//   stress  core 0 fills the screen over SPI without pause, core 1 runs float
//           math without pause
//   clock   clock animation at 60 FPS

#include <Arduino.h>
#include <hardware/clocks.h>
#include "LGFX_Config.hpp"
#include "ClockFace.hpp"

struct Step { uint8_t backlight; uint16_t screen; bool stress; bool clock; };

const Step STEPS[] = {
  {   0, TFT_WHITE, false, false },  // L1
  {   0, TFT_WHITE, true,  false },  // L2
  {  25, TFT_WHITE, false, false },  // L3
  {  50, TFT_WHITE, false, false },  // L4
  {  75, TFT_WHITE, false, false },  // L5
  { 100, TFT_WHITE, false, false },  // L6
  { 100, TFT_WHITE, true,  false },  // L7
  { 100, TFT_BLACK, false, false },  // L8
  { 100, TFT_BLACK, false, true  },  // L9
};
constexpr int NUM_STEPS = sizeof(STEPS) / sizeof(STEPS[0]);
constexpr uint32_t STEP_MS = 20'000;
constexpr int16_t LABEL_TOP = 200;   // stress fills everything above the label

LGFX display;
ClockFace clockFace(display, 110, TFT_RED, TFT_WHITE, 8, 4);

int current = -1;
volatile bool stress = false;        // read by core 1
volatile float sink = 0.0f;          // keeps core 1's math from being optimised away
uint32_t lastFrame = 0;

void enterStep(int i) {
  const Step& s = STEPS[i];
  analogWrite(PIN_BL, s.backlight);
  stress = s.stress;
  if (s.clock) {                     // no label, the clock face is obvious
    clockFace.begin();               // first frame covers the whole screen
    return;
  }
  display.fillScreen(s.screen);

  char label[4];
  snprintf(label, sizeof(label), "L%d", i + 1);
  display.setTextColor(s.screen == TFT_WHITE ? TFT_BLACK : TFT_WHITE);
  display.setTextDatum(middle_center);
  display.setTextSize(2);
  display.drawString(label, 120, 220);
}

void setup() {
  // Peripheral clock to clk_sys (133 MHz) for the fast SPI clock, as in phase 1.1
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys), clock_get_hz(clk_sys));

  // RS485 in receive mode, as on a real slave
  pinMode(14, OUTPUT); digitalWrite(14, LOW);   // DEsrc/main.cpp
  pinMode(15, OUTPUT); digitalWrite(15, LOW);   // RE

  analogWriteRange(100);             // backlight duty in percent
  display.init();
}

void loop() {
  const uint32_t now = millis();
  const int i = min<int>(now / STEP_MS, NUM_STEPS - 1);
  if (i != current) {
    current = i;
    enterStep(i);
  }

  const Step& s = STEPS[i];
  if (s.stress) {
    display.fillRect(0, 0, 240, LABEL_TOP, s.screen);
  } else if (s.clock) {
    if (now - lastFrame >= 16) {
      lastFrame = now;
      clockFace.prepareFrame(now * 0.04f, now * 0.12f);   // 40 and 120 deg/s
      clockFace.pushFrame();
    }
  } else {
    delay(10);
  }
}

// Core 1: float math in stress steps, idle otherwise
void loop1() {
  if (!stress) { delay(10); return; }
  float x = sink;
  for (int i = 0; i < 1000; i++) x = sinf(x + 0.001f) * 1.0001f + cosf(x);
  sink = x;
}
