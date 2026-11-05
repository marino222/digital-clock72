// Phase 5.1, steps F2-F5: minimal bring-up sketch for the prototype PCB.
// Prints a line over USB serial once a second, so flashing, serial output
// and booting from flash can be checked without any other hardware.
//
// Arduino IDE: board "Raspberry Pi Pico" from the earlephilhower core
// (Boards Manager URL: https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json)

uint32_t counter = 0;

void setup() {
  Serial.begin(115200);
  // No while(!Serial) here: for F4 the board has to run without a monitor attached
}

void loop() {
  Serial.print("digital-clock72 bring-up | #");
  Serial.print(counter++);
  Serial.print(" | uptime ");
  Serial.print(millis() / 1000);
  Serial.print(" s | CPU ");
  Serial.print(rp2040.f_cpu() / 1000000);
  Serial.println(" MHz");
  delay(1000);
}
