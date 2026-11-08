// Phase 5.1, section H, steps H5-H7: backlight PWM test for the node PCB.
//
// Same clock-hand animation as phase 1.1, so the panel always shows content,
// plus hardware PWM on GPIO21 (BL_PWM -> R13 -> Q1 -> LEDK).
//
//  H6  After reset, GPIO21 is left undriven for STARTUP_HOLD_MS while the
//      display is already initialised and showing the clock face. Only R14
//      (and the RP2040's internal pad pull-down) hold Q1 off. The backlight
//      must stay dark for the whole hold. Any glow means the base is not held
//      low.
//  H5  After the hold, the firmware starts in SWEEP mode: duty ramps
//      0 -> 100 -> 0 % as a triangle. Brightness should change smoothly.
//  H7  Use "d 50" for a fixed 50 % duty, then probe LEDK. Change the frequency
//      with "f <Hz>" to check the switching at different PWM frequencies.
//
// Serial commands (end each with Enter):
//   s          sweep mode (triangle 0..100..0 %)
//   t          step mode (0, 10, ... 100 %, STEP_HOLD_MS each)
//   d <pct>    fixed duty in percent, e.g. "d 50" or "d 2.5"
//   f <Hz>     PWM frequency, PWM_FREQ_MIN_HZ..PWM_FREQ_MAX_HZ
//   p <ms>     sweep period (full 0..100..0 cycle)
//   g          toggle gamma correction for sweep/step (off = linear duty)
//   ?          help and current settings

#include <Arduino.h>
#include <cmath>
#include <hardware/clocks.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include "LGFX_Config.hpp"
#include "ClockFace.hpp"

LGFX display;

// clock face geometry (identical to phase 1.1)
constexpr int16_t CX = 120, CY = 120, RADIUS = 110;
constexpr uint8_t HAND1_THICK = 8, HAND2_THICK = 4;

ClockFace clockFace(display, RADIUS, TFT_RED, TFT_WHITE, HAND1_THICK, HAND2_THICK);

// Hand rotation speeds in degrees per second
constexpr float HAND1_DEG_S = 40.0f;
constexpr float HAND2_DEG_S = 120.0f;

float angle_1 = 0.0f, angle_2 = 0.0f;

// ---------------------------------------------------------------------------
// Backlight test parameters
// ---------------------------------------------------------------------------
constexpr uint32_t STARTUP_HOLD_MS   = 5000;  // H6: backlight undriven after reset
constexpr uint32_t SERIAL_WAIT_MS    = 3000;  // don't block forever without a monitor
constexpr uint32_t PWM_FREQ_DEFAULT  = 1000;  // Hz
constexpr uint32_t PWM_FREQ_MIN_HZ   = 10;
constexpr uint32_t PWM_FREQ_MAX_HZ   = 200000;
constexpr uint32_t SWEEP_PERIOD_DEF  = 6000;  // ms for 0 -> 100 -> 0 %
constexpr uint32_t STEP_HOLD_MS      = 2000;  // ms per step in step mode
constexpr float    GAMMA             = 2.2f;

enum class Mode { Sweep, Step, Fixed };

Mode     mode         = Mode::Sweep;
bool     gammaOn      = false;
float    fixedDuty    = 0.5f;          // 0..1
uint32_t sweepPeriod  = SWEEP_PERIOD_DEF;
uint32_t modeStart    = 0;             // millis() when the current mode started
float    currentDuty  = 0.0f;          // last duty written to the PWM, 0..1

// PWM state
uint     pwmSlice   = 0;
uint     pwmChannel = 0;
uint32_t pwmWrap    = 0;               // counter top, period = wrap + 1 counts
uint32_t pwmDiv     = 1;               // integer clock divider
uint32_t pwmFreqSet = PWM_FREQ_DEFAULT;

// Report statistics
uint32_t frameCount = 0;
uint32_t lastReport = 0;
uint32_t lastFrameTime = 0;

// ---------------------------------------------------------------------------
// PWM helpers
// ---------------------------------------------------------------------------

// Sets the PWM frequency using an integer divider only. A fractional divider
// would make the period jitter by one clock, which shows up on the scope (H7).
void pwmSetFrequency(uint32_t freqHz) {
  freqHz = constrain(freqHz, PWM_FREQ_MIN_HZ, PWM_FREQ_MAX_HZ);
  const uint32_t sysHz = clock_get_hz(clk_sys);
  const uint32_t counts = sysHz / freqHz;              // counts per period at div 1

  pwmDiv  = (counts + 65535) / 65536;                  // smallest div that fits 16 bit
  if (pwmDiv < 1)   pwmDiv = 1;
  if (pwmDiv > 255) pwmDiv = 255;
  pwmWrap = sysHz / (pwmDiv * freqHz) - 1;
  if (pwmWrap > 65535) pwmWrap = 65535;
  pwmFreqSet = freqHz;

  pwm_set_clkdiv_int_frac(pwmSlice, pwmDiv, 0);
  pwm_set_wrap(pwmSlice, pwmWrap);
}

float pwmActualFrequency() {
  return clock_get_hz(clk_sys) / float(pwmDiv * (pwmWrap + 1));
}

// duty 0..1. Level 0 = always low, level wrap+1 = always high.
void pwmSetDuty(float duty) {
  duty = constrain(duty, 0.0f, 1.0f);
  currentDuty = duty;
  uint32_t level = uint32_t(lroundf(duty * (pwmWrap + 1)));
  pwm_set_chan_level(pwmSlice, pwmChannel, level);
}

void pwmBegin() {
  gpio_set_function(PIN_BL, GPIO_FUNC_PWM);
  pwmSlice   = pwm_gpio_to_slice_num(PIN_BL);
  pwmChannel = pwm_gpio_to_channel(PIN_BL);
  pwm_set_chan_level(pwmSlice, pwmChannel, 0);   // start dark
  pwmSetFrequency(PWM_FREQ_DEFAULT);
  pwm_set_enabled(pwmSlice, true);
}

// Position 0..1 of the sweep/step pattern -> duty, with optional gamma so the
// perceived brightness ramps linearly instead of the duty cycle.
float shape(float pos) {
  return gammaOn ? powf(pos, GAMMA) : pos;
}

float dutyForMode(uint32_t nowMs) {
  const uint32_t t = nowMs - modeStart;
  switch (mode) {
    case Mode::Sweep: {
      float phase = float(t % sweepPeriod) / sweepPeriod;          // 0..1
      float pos = phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f; // triangle
      return shape(pos);
    }
    case Mode::Step: {
      uint32_t step = (t / STEP_HOLD_MS) % 11;                     // 0..10
      return shape(step / 10.0f);
    }
    case Mode::Fixed:
    default:
      return fixedDuty;
  }
}

const char* modeName() {
  switch (mode) {
    case Mode::Sweep: return "sweep";
    case Mode::Step:  return "step";
    default:          return "fixed";
  }
}

// ---------------------------------------------------------------------------
// Serial commands
// ---------------------------------------------------------------------------
void printSettings() {
  Serial.printf("mode:%s  duty:%.1f%%  freq set:%lu Hz actual:%.2f Hz  "
                "div:%lu wrap:%lu (%lu steps)  gamma:%s  sweep:%lu ms\n",
                modeName(), currentDuty * 100.0f,
                pwmFreqSet, pwmActualFrequency(),
                pwmDiv, pwmWrap, pwmWrap + 1,
                gammaOn ? "on" : "off", sweepPeriod);
}

void printHelp() {
  Serial.println("Commands: s=sweep  t=step  d <pct>=fixed duty  f <Hz>=frequency  "
                 "p <ms>=sweep period  g=toggle gamma  ?=help");
  printSettings();
}

void handleCommand(char* line) {
  while (*line == ' ') line++;
  if (*line == '\0') return;

  const char cmd = line[0];
  const char* arg = line + 1;

  switch (cmd) {
    case 's': mode = Mode::Sweep; modeStart = millis(); break;
    case 't': mode = Mode::Step;  modeStart = millis(); break;
    case 'd': {
      float pct = atof(arg);
      fixedDuty = constrain(pct, 0.0f, 100.0f) / 100.0f;
      mode = Mode::Fixed;
      break;
    }
    case 'f': {
      long hz = atol(arg);
      if (hz <= 0) { Serial.println("usage: f <Hz>"); return; }
      pwmSetFrequency(uint32_t(hz));
      break;
    }
    case 'p': {
      long ms = atol(arg);
      if (ms < 200) { Serial.println("usage: p <ms>, min 200"); return; }
      sweepPeriod = uint32_t(ms);
      break;
    }
    case 'g': gammaOn = !gammaOn; break;
    case '?': case 'h': printHelp(); return;
    default:
      Serial.printf("unknown command '%c'\n", cmd);
      printHelp();
      return;
  }
  pwmSetDuty(dutyForMode(millis()));   // apply immediately so the print is current
  printSettings();
}

void pollSerial() {
  static char buf[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      buf[len] = '\0';
      handleCommand(buf);
      len = 0;
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

// ---------------------------------------------------------------------------
void setup() {
  // 1. MUST BE FIRST: Route the peripheral clock to 133 MHz for high-speed SPI
  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys),
                  clock_get_hz(clk_sys));

  // GPIO21 is deliberately NOT touched here (H6). It stays an input, as after
  // reset, so only R14 holds Q1 off.

  Serial.begin(921600); //serial output for reporting of measurements via USB-C
  const uint32_t serialStart = millis();
  while (!Serial && millis() - serialStart < SERIAL_WAIT_MS) delay(10);

  display.init();
  display.setRotation(0);

  // Draw the clock face now, so the panel has content during the hold. If the
  // backlight leaked, the hands would be visible.
  clockFace.begin();
  clockFace.prepareFrame(angle_1, angle_2);
  clockFace.pushFrame();

  Serial.printf("\nBacklight PWM test (H5-H7). GPIO%d undriven for %lu ms (H6): "
                "backlight must stay dark.\n", PIN_BL, STARTUP_HOLD_MS);
  const uint32_t holdStart = millis();
  uint32_t lastTick = UINT32_MAX;
  while (millis() - holdStart < STARTUP_HOLD_MS) {
    uint32_t remaining = (STARTUP_HOLD_MS - (millis() - holdStart) + 999) / 1000;
    if (remaining != lastTick) {
      Serial.printf("  H6 hold: %lu s, GPIO%d level read back: %d\n",
                    remaining, PIN_BL, digitalRead(PIN_BL));
      lastTick = remaining;
    }
    delay(10);
  }

  pwmBegin();
  Serial.println("H6 hold done, PWM enabled.");
  modeStart = millis();
  pwmSetDuty(dutyForMode(modeStart));
  printHelp();

  lastReport = millis();
  lastFrameTime = micros();
}

void loop() {
  pollSerial();

  uint32_t nowMicros = micros();
  float dt = (nowMicros - lastFrameTime) / 1e6f;
  lastFrameTime = nowMicros;

  angle_1 = fmodf(angle_1 + HAND1_DEG_S * dt, 360.0f);
  angle_2 = fmodf(angle_2 + HAND2_DEG_S * dt, 360.0f);

  clockFace.prepareFrame(angle_1, angle_2);
  clockFace.pushFrame();
  frameCount++;

  uint32_t now = millis();
  pwmSetDuty(dutyForMode(now));   // the PWM latches new levels at wrap, no glitch

  //report once a second
  if (now - lastReport >= 1000) {
    float fps = frameCount * 1000.0f / (now - lastReport);
    Serial.printf("FPS:%.1f  ", fps);
    printSettings();
    frameCount = 0;
    lastReport = now;
  }
}
