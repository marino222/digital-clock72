// Phase 5.1, section J: RS485 sender for the prototype PCB.
// Sends a counter 0, 1, 2 ... 255, 0, 1 ... over RS485 as fast as possible.
// The receiver sketch checks that no byte is missing or wrong.
// Also toggles ADDR_OUT and SPARE_OUT once a second for J5/J6.
//
// Arduino IDE: board "Raspberry Pi Pico" from the earlephilhower core

#define BAUD 1000000  // change to 1000000 for the 1 Mbaud test, same on both boards

const int PIN_TX = 12;
const int PIN_DE = 14;         // high = driver on
const int PIN_RE = 15;         // high = receiver off
const int PIN_SPARE_OUT = 8;
const int PIN_ADDR_OUT = 10;

uint8_t counter = 0;
unsigned long lastToggle = 0;
bool lineLevel = false;

void setup() {
  
  Serial.begin(115200);

  pinMode(PIN_DE, OUTPUT);
  pinMode(PIN_RE, OUTPUT);
  digitalWrite(PIN_DE, HIGH);
  digitalWrite(PIN_RE, HIGH);

  pinMode(PIN_ADDR_OUT, OUTPUT);
  pinMode(PIN_SPARE_OUT, OUTPUT);

  Serial1.setTX(PIN_TX);
  Serial1.begin(BAUD);
}

void loop() {
  Serial1.write(counter++);  // waits if the UART is busy, so this runs at full line speed

  if (millis() - lastToggle >= 1000) {
    lastToggle = millis();
    lineLevel = !lineLevel;
    digitalWrite(PIN_ADDR_OUT, lineLevel);
    digitalWrite(PIN_SPARE_OUT, lineLevel);
    Serial.print("sending at ");
    Serial.print(BAUD);
    Serial.print(" Bd | ADDR_OUT and SPARE_OUT = ");
    Serial.println(lineLevel);
  }
}
