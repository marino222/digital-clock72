#include "LGFX_Config.hpp"
#include "ClockFace.hpp"

LGFX display;
ClockFace clockFace(display, 110, TFT_RED, TFT_WHITE, TFT_CYAN, 6, 4, 2);

uint32_t frameCount = 0;
uint32_t lastFpsReport = 0;

void setup() {
  Serial.begin(115200);
  display.init();
  display.setRotation(0);
  display.setBrightness(255);
  clockFace.begin();
  lastFpsReport = millis();
}

void loop() {
  uint32_t now = millis();
  float t = now / 1000.0f;                    // 1 real second == 1 virtual minute
  clockFace.update(t / 60.0f, t, t * 60.0f);

  frameCount++;
  if (now - lastFpsReport >= 1000) {
    Serial.printf("FPS: %.1f\n", frameCount * 1000.0f / (now - lastFpsReport)); 
    frameCount = 0;
    lastFpsReport = now;
  }
}
