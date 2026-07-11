
#include "LGFX_Config.hpp"


LGFX display;


void setup() {

  pinMode(LED_BUILTIN, OUTPUT);

  display.init();
  display.setRotation(0);
  display.setBrightness(255);

  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_WHITE);
  display.setTextSize(2);
  display.setCursor(40, 110);
  display.println("Hello GC9A01");

  display.fillCircle(120, 60, 20, TFT_RED);

}

void loop() {

  // simple pulse to prove it's alive and not just a static init
  static uint8_t hue = 0;
  display.fillCircle(120, 180, 15, display.color565(hue, 255 - hue, 128));
  hue += 4;
  delay(50);
}

