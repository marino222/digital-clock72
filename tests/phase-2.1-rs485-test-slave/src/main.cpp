#include <Arduino.h>
#include <hardware/clocks.h>
#include "protocol.h"
#include "pin_definitions.h"
#include "LGFX_Config.hpp"
#include "ClockFace.hpp"

LGFX display;

// clock face geometry
const int16_t CX = 120, CY = 120, RADIUS = 110;
const float HAND1_LEN = RADIUS * 0.6f, HAND2_LEN = RADIUS * 0.9f;
const uint8_t HAND1_THICK = 8, HAND2_THICK = 4;

ClockFace clockFace(display, RADIUS, TFT_RED, TFT_WHITE, HAND1_THICK, HAND2_THICK);



void updateClock(uint16_t angle1DeciDeg, uint16_t angle2DeciDeg) {
  clockFace.prepareFrame(angle1DeciDeg, angle2DeciDeg);
  clockFace.pushFrame();

}


void setup() {

  // bind the peripheral clock to the system clock (133 MHz)
  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys),
                  clock_get_hz(clk_sys));

  
  // 1. Start USB Serial for debugging messages on your computer screen
  Serial.begin(115200);
  while (!Serial && millis() < 2000); // Wait up to 2 seconds for USB serial to connect
  Serial.println("RS485 Slave Initializing...");

  // 2. Configure the Direction Control Pin for permanent listening
  pinMode(RS485_CTRL_PIN, OUTPUT);
  digitalWrite(RS485_CTRL_PIN, LOW); // Lock into Receive mode

  // 3. Setup Hardware Serial (UART0) pins and baud rate
  Serial1.setTX(RS485_TX_PIN);
  Serial1.setRX(RS485_RX_PIN);
  Serial1.begin(RS485_BAUD_RATE);

  Serial.println("Slave ready. Listening for packets...");

  display.init();
  display.setRotation(0);
  display.setBrightness(255);
  
  // Initialize the clock face (this triggers the first fillScreen)
  clockFace.begin();


}

void loop() {
  // Continuously parse incoming bytes one by one to find sync markers safely
  static enum { WAITING_FOR_SYNC1, WAITING_FOR_SYNC2, READING_PAYLOAD } state = WAITING_FOR_SYNC1;
  static ClockDataPacket rxPacket;
  static size_t bytesReadIntoPacket = 0;

  while (Serial1.available() > 0) {
    uint8_t inByte = Serial1.read();

    switch (state) {
      case WAITING_FOR_SYNC1:
        if (inByte == SYNC_BYTE_1) {
          rxPacket.startByte1 = inByte;
          state = WAITING_FOR_SYNC2;
        }
        break;

      case WAITING_FOR_SYNC2:
        if (inByte == SYNC_BYTE_2) {
          rxPacket.startByte2 = inByte;
          // We found both sync bytes! Now we read the REST of the struct.
          // Point to everything AFTER the two sync bytes:
          bytesReadIntoPacket = 0;
          state = READING_PAYLOAD;
        } else {
          // If the second byte wasn't 0x55, maybe this byte IS the new 0xAA?
          if (inByte == SYNC_BYTE_1) {
            state = WAITING_FOR_SYNC2;
          } else {
            state = WAITING_FOR_SYNC1;
          }
        }
        break;

      case READING_PAYLOAD: {
        // Calculate how many bytes remain after the 2 sync bytes
        uint8_t* targetPtr = (uint8_t*)&rxPacket.packetSequence;
        size_t totalPayloadSize = sizeof(ClockDataPacket) - 2;

        // Put the current byte directly into our struct buffer
        targetPtr[bytesReadIntoPacket++] = inByte;

        // Have we collected the entire rest of the packet?
        if (bytesReadIntoPacket >= totalPayloadSize) {
          // Reset state machine for the next packet
          state = WAITING_FOR_SYNC1;

          // Verify Checksum
          size_t bytesToCalculate = sizeof(ClockDataPacket) - 1;
          uint8_t calculatedCheck = calculateChecksum((uint8_t*)&rxPacket, bytesToCalculate);

          if (calculatedCheck == rxPacket.xorChecksum) {

            // The master does the angle math and sends tenths of a degree,
            // so the received values go straight to the display with no
            // conversion.
            updateClock(rxPacket.angle1DeciDeg, rxPacket.angle2DeciDeg);


            Serial.print("[SUCCESS] Pkt #");
            Serial.print(rxPacket.packetSequence);
            Serial.print(" | Uptime: ");
            Serial.print(rxPacket.uptimeMillis);
            Serial.print("ms | Angles: ");
            Serial.print(rxPacket.angle1DeciDeg / 10.0f, 1);
            Serial.print("/");
            Serial.print(rxPacket.angle2DeciDeg / 10.0f, 1);
            Serial.print(" deg | Msg: ");
            Serial.println(rxPacket.statusMessage);
          } else {
            Serial.println("[ERROR] Checksum mismatch!");
          }
        }
        break;
      }
    }
  }
}

