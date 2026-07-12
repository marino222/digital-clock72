#include "LGFX_Config.hpp"
#include "ClockFace.hpp"

LGFX display;
ClockFace clockFace(display, 110, TFT_RED, TFT_CYAN, 8, 4);

constexpr float SPEED = 1.0f;
constexpr float HAND1_RATE_DEG_S = 20.0f;
constexpr float HAND2_RATE_DEG_S = 90.0f;

float virtualTime = 0.0f;
uint32_t lastFrameTime = 0;
uint32_t lastReport = 0;

void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(0);
  display.setBrightness(255);
  clockFace.begin();

  lastFrameTime = micros();
  lastReport = millis();
}

void loop() {
  uint32_t nowMicros = micros();
  float dtSeconds = (nowMicros - lastFrameTime) / 1e6f;
  lastFrameTime = nowMicros;

  virtualTime += dtSeconds * SPEED;
  float angle1 = fmodf(virtualTime * HAND1_RATE_DEG_S, 360.0f);
  float angle2 = fmodf(virtualTime * HAND2_RATE_DEG_S, 360.0f);
  clockFace.update(angle1, angle2);

  uint32_t nowMillis = millis();
  if (nowMillis - lastReport >= 500) {
    Serial.printf("FPS: %.1f\n", 1.0f / dtSeconds);
    lastReport = nowMillis;
  }
}
