// Phase 5.1, steps G1-G3: flashing and debugging the prototype PCB over SWD.
// Same idea as phase-5.1-serial-hello, but split into small functions so
// there is something to put breakpoints on, step into and inspect.
//
// Suggested breakpoints (G3):
//   - the line in loop() that calls fibonacci()  -> step into it with F11
//   - inside fibonacci()                         -> watch a, b, next change
//   - the `if (counter % 10 == 0)` branch        -> only hits every 10 s
//
// Note: while the core is halted, USB is not serviced, so the serial monitor
// may disconnect after a few seconds at a breakpoint. It reconnects on resume.

#include <Arduino.h>

uint32_t counter = 0;

uint32_t fibonacci(uint32_t n) {
  uint32_t a = 0;
  uint32_t b = 1;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t next = a + b;
    a = b;
    b = next;
  }
  return a;
}

void printStatus(uint32_t fib) {
  Serial.print("digital-clock72 SWD test | #");
  Serial.print(counter);
  Serial.print(" | uptime ");
  Serial.print(millis() / 1000);
  Serial.print(" s | fib(");
  Serial.print(counter % 40);
  Serial.print(") = ");
  Serial.println(fib);
}

void setup() {
  Serial.begin(115200);
  // No while(!Serial) here: the board has to run without a monitor attached
}

void loop() {
  uint32_t fib = fibonacci(counter % 40);
  printStatus(fib);

  if (counter % 10 == 0) {
    Serial.println("-- 10 s marker --");
  }

  counter++;
  delay(1000);
}
