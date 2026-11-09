// Phase 5.1, sections I and J: RS485 receiver for the prototype PCB.
// Expects the counter from the sender sketch: every byte must be the previous
// one plus 1. Anything else (a wrong byte, a missing byte) counts as an error.
// Prints the result once a second, together with ADDR_IN and SPARE_IN.
//
// Arduino IDE: board "Raspberry Pi Pico" from the earlephilhower core

#define BAUD 1000000  // change to 1000000 for the 1 Mbaud test, same on both boards

const int PIN_RX = 13;
const int PIN_DE = 14;         // low = driver off
const int PIN_RE = 15;         // low = receiver on
const int PIN_ADDR_IN = 9;
const int PIN_SPARE_IN = 11;

uint8_t expected = 0;
bool firstByte = true;
unsigned long bytesThisSecond = 0;
unsigned long totalBytes = 0;
unsigned long errors = 0;
unsigned long lastPrint = 0;

void setup() {
  Serial.begin(115200);

  pinMode(PIN_DE, OUTPUT);
  pinMode(PIN_RE, OUTPUT);
  digitalWrite(PIN_DE, LOW);
  digitalWrite(PIN_RE, LOW);

  pinMode(PIN_ADDR_IN, INPUT_PULLDOWN);
  pinMode(PIN_SPARE_IN, INPUT_PULLDOWN);

  Serial1.setRX(PIN_RX);
  Serial1.setFIFOSize(4096);  // room for 40 ms of data at 1 Mbaud while we print
  Serial1.begin(BAUD);
}

void loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    if (!firstByte && b != expected) {
      errors++;
    }
    expected = b + 1;
    firstByte = false;
    bytesThisSecond++;
    totalBytes++;
  }

  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print(bytesThisSecond);
    Serial.print(" B/s | total ");
    Serial.print(totalBytes);
    Serial.print(" | errors ");
    Serial.print(errors);
    Serial.print(" | ADDR_IN ");
    Serial.print(digitalRead(PIN_ADDR_IN));
    Serial.print(" SPARE_IN ");
    Serial.println(digitalRead(PIN_SPARE_IN));
    bytesThisSecond = 0;
  }
}
