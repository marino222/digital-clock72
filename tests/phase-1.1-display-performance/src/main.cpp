#include <Arduino.h>
#include <cmath>
#include <hardware/clocks.h>
#include "LGFX_Config.hpp"
#include "ClockFace.hpp"

LGFX display;

// clock face geometry
constexpr int16_t CX = 120, CY = 120, RADIUS = 110;
constexpr float HAND1_LEN = RADIUS * 0.6f, HAND2_LEN = RADIUS * 0.9f;
constexpr uint8_t HAND1_THICK = 8, HAND2_THICK = 4;

ClockFace clockFace(display, RADIUS, TFT_RED, TFT_WHITE, HAND1_THICK, HAND2_THICK);

// Hand rotation speeds in degrees per second
constexpr float HAND1_DEG_S = 20.0f;
constexpr float HAND2_DEG_S = 90.0f;

float angle_1 = 0.0f, angle_2 = 0.0f;

uint32_t frameCount = 0; //calculate FPS
uint32_t sumDraw = 0, sumPush = 0; //time needed to push sprite to display
uint32_t maxDraw = 0, maxPush = 0; //time needed to draw/calculate pixels and draw the to sprite
uint32_t lastReport = 0; //track time elapsed since last report
uint32_t lastFrameTime = 0; //track time elapsed since last frame

// Track the time the reporting block itself takes
uint32_t lastReportDuration = 0; 

// Track the size of the sprite data payload in bytes
uint32_t spriteSizeBytes = 0;

void setup() {
  // 1. MUST BE FIRST: Route the peripheral clock to 133 MHz for high-speed SPI
  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys),
                  clock_get_hz(clk_sys));

  Serial.begin(921600); //serial output for reporting of measurements via USB-C
  while (!Serial) delay(10);

  display.init();
  display.setRotation(0);
  display.setBrightness(255);
  clockFace.begin();

  lastReport = millis();
  lastFrameTime = micros();
}

void loop() {
  uint32_t nowMicros = micros();
  float dt = (nowMicros - lastFrameTime) / 1e6f;
  lastFrameTime = nowMicros;

  angle_1 = fmodf(angle_1 + HAND1_DEG_S * dt, 360.0f);
  angle_2 = fmodf(angle_2 + HAND2_DEG_S * dt, 360.0f);
  
  uint32_t t0 = micros();

  //draw to sprite
  clockFace.update(angle_1, angle_2);

  uint32_t t1 = micros();

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
    uint32_t reportStart = micros(); // Start timing the report block

    float fps = frameCount * 1000.0f / (now - lastReport);
    float avgDraw = sumDraw / (float)frameCount;
    float avgPush = sumPush / (float)frameCount;

    // We print the duration of the PREVIOUS report block here, along with the payload size
    Serial.printf("FPS:%.1f  draw avg:%.2fms max:%.2fms  push avg:%.2fms max:%.2fms  [prev report: %.2fms, payload: %lu kB]\n",
                  fps, 
                  avgDraw / 1000.0f, 
                  maxDraw / 1000.0f, 
                  avgPush / 1000.0f, 
                  maxPush / 1000.0f, 
                  lastReportDuration / 1000.0f, 
                  spriteSizeBytes / 1024);
    
    //clear accumulated values              
    frameCount = 0;
    sumDraw = 0; sumPush = 0;
    maxDraw = 0; maxPush = 0;
    lastReport = now;

    // End timing the report block
    lastReportDuration = micros() - reportStart; 
  }
}