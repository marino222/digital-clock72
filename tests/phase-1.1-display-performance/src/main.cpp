
#include "LGFX_Config.hpp"
#include "angle_source.h"
#include "clock_face.h"
#include "fps_counter.h"

namespace {
LGFX display;
ClockFace clockFace(display);
AngleSource angleSource;
FpsCounter fps;
uint32_t lastFrameUs = 0;

void runFrame() {
  float targetAngle;
  if (angleSource.poll(targetAngle)) {
    clockFace.setTargetAngle(targetAngle);
  }

  uint32_t now = micros();
  uint32_t dt = now - lastFrameUs;
  lastFrameUs = now;

  clockFace.update(dt);
  clockFace.render();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  uint32_t waitStartMs = millis();
  while (!Serial && millis() - waitStartMs < 3000) {
    // give a USB serial monitor a few seconds to attach, but don't block forever
  }

  display.init();
  display.setRotation(0);

  clockFace.begin();
  angleSource.begin();
  lastFrameUs = micros();

  Serial.println("Phase 1.1 display performance test");
  Serial.println("Send 'A<degrees>' (e.g. A135) on Serial to set the hand angle manually.");

  fps.runBenchmarkWindow(10000, "hand-animation", runFrame);
}

void loop() {
  fps.beginFrame();
  runFrame();
  fps.endFrame();
  fps.maybeReport(1000);
}
