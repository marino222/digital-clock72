#include <Arduino.h>
#include <cmath>
#include "LGFX_Config.hpp"

LGFX display;
LGFX_Sprite frame(&display);

// clock face geometry
constexpr int16_t CX = 120, CY = 120, RADIUS = 110;
constexpr float HAND1_LEN = RADIUS * 0.6f, HAND2_LEN = RADIUS * 0.9f;
constexpr uint8_t HAND1_THICK = 8, HAND2_THICK = 4;

// Hand rotation speeds in degrees per second
constexpr float HAND1_DEG_S = 20.0f;
constexpr float HAND2_DEG_S = 90.0f;

float angle_1 = 0.0f, angle_2 = 0.0f;

uint32_t frameCount = 0; //calculate FPS
uint32_t sumDraw = 0, sumPush = 0; //time needed to push sprite to display
uint32_t maxDraw = 0, maxPush = 0; //time needed to draw/calculate pixels and draw the to sprite
uint32_t lastReport = 0; //track time elapsed since last report
uint32_t lastFrameTime = 0; //track time elapsed since last frame


/*draws clock hand to sprite*/
void drawHand(float angle, float length, uint8_t thickness, uint16_t color) {
  float rad = angle * (M_PI / 180.0f);
  int16_t x = CX + length * sinf(rad);
  int16_t y = CY - length * cosf(rad);
  frame.drawWedgeLine(CX, CY, x, y, thickness / 2.0f, thickness / 2.0f, color);
} 



void setup() {
  Serial.begin(921600); //serial output for reporting of measurements via USB-C


  display.init();
  display.setRotation(0);
  display.setBrightness(255);

  frame.setColorDepth(16);
  frame.createSprite(display.width(), display.height());

  lastReport = millis();
  lastFrameTime = micros();
}

void loop() {
  uint32_t nowMicros = micros();
  float dt = (nowMicros - lastFrameTime) / 1e6f; //time the last execution of loop() took in seconds
  lastFrameTime = nowMicros;

  
  angle_1 = fmodf(angle_1 + HAND1_DEG_S * dt, 360.0f);
  angle_2 = fmodf(angle_2 + HAND2_DEG_S * dt, 360.0f);
  
  uint32_t t0 = micros();

  //draw to sprite
  frame.fillScreen(TFT_BLACK);
  drawHand(angle_1, HAND1_LEN, HAND1_THICK, TFT_RED);
  drawHand(angle_2, HAND2_LEN, HAND2_THICK, TFT_WHITE);

  uint32_t t1 = micros();

  display.startWrite();
  frame.pushSprite(&display, 0, 0);
  display.endWrite();

  uint32_t t2 = micros();

  //calculate how long this took
  uint32_t drawTime = t1 - t0;
  uint32_t pushTime = t2 - t1;

  frameCount++;
  sumDraw += drawTime;
  sumPush += pushTime;
  maxDraw = std::max(maxDraw, drawTime);
  maxPush = std::max(maxPush, pushTime);

  //report results every second
  uint32_t now = millis();
  if (now - lastReport >= 1000) {
    float fps = frameCount * 1000.0f / (now - lastReport);
    float avgDraw = sumDraw / (float)frameCount;
    float avgPush = sumPush / (float)frameCount;

    Serial.printf("FPS:%.1f  draw avg:%.0fus max:%luus  push avg:%.0fus max:%luus\n",
                  fps, avgDraw, maxDraw, avgPush, maxPush);
    
    //clear accumulated values              
    frameCount = 0;
    sumDraw = 0; sumPush = 0;
    maxDraw = 0; maxPush = 0;
    lastReport = now;
  }

}
