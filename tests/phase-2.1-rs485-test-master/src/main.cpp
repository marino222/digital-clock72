#include <Arduino.h>
#include "pin_definitions.h"
#include "protocol.h"


const unsigned long SEND_INTERVAL_MS = 16; // Send a packet every 16 ms (~60 Hz)

// Hand rotation speeds, matching the phase-1.1 display test (40 and 120 deg/s).
//
// The angles are accumulated in MILLIdegrees, not whole degrees. At 60 Hz the
// slower hand only moves ~0.64 deg per packet, so a whole-degree accumulator
// would truncate every step to 0 and the hand would never move. Millidegrees
// give the accumulator enough headroom to keep integer math exact, and the
// value is divided down to whole degrees only when the packet is filled.
//
// Handy coincidence: millidegrees-per-millisecond is the same number as
// degrees-per-second, so these constants read as their real-world speed.
const uint32_t HAND1_MDEG_PER_MS = 20;    //  deg/s
const uint32_t HAND2_MDEG_PER_MS = 100;   //  deg/s
const uint32_t FULL_TURN_MDEG = 360000;   // one revolution

void setup() {

  // bind the peripheral clock to the system clock (133 MHz)
  clock_configure(clk_peri,
                  0,
                  CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  clock_get_hz(clk_sys),
                  clock_get_hz(clk_sys));

  // 1. Start USB Serial for communication with PC
  Serial.begin(115200);
  while (!Serial && millis() < 2000); // Wait up to 2 seconds for USB serial to connect
  Serial.println("RS485 Master Initializing...");

  // 2. Configure the RS485 module for transmit mode
  pinMode(RS485_CTRL_PIN, OUTPUT);
  digitalWrite(RS485_CTRL_PIN, HIGH);

  // 3. Setup Hardware Serial (UART0) pins and baud rate
  Serial1.setTX(RS485_TX_PIN);
  Serial1.setRX(RS485_RX_PIN);
  Serial1.begin(RS485_BAUD_RATE);

  Serial.println("Master ready. Broadcasting packets...");

}


void loop() {
  static uint32_t sequenceCounter = 0;
  // Initialized on the first call to loop(), i.e. after setup() has already
  // burned up to 2 seconds waiting for USB serial. Starting from 0 instead
  // would make the first packet report that whole wait as elapsed time and
  // jump the hands most of a revolution.
  static unsigned long lastSendTime = millis();

  // Current hand positions, in millidegrees (see the constants above).
  static uint32_t angle1Milli = 0, angle2Milli = 0;

  // Control send rate
  unsigned long now = millis();
  if (now - lastSendTime >= SEND_INTERVAL_MS) {
    // How much time actually passed since the last packet. Using the real
    // elapsed time, rather than assuming exactly SEND_INTERVAL_MS, keeps the
    // hands turning at the right speed even if a loop runs late.
    unsigned long elapsedMs = now - lastSendTime;
    lastSendTime = now;

    // Advance both hands, wrapping at a full revolution.
    angle1Milli = (angle1Milli + HAND1_MDEG_PER_MS * elapsedMs) % FULL_TURN_MDEG;
    angle2Milli = (angle2Milli + HAND2_MDEG_PER_MS * elapsedMs) % FULL_TURN_MDEG;

    // 1. Instantiate the packet object
    ClockDataPacket txPacket;

    // 2. Fill the header framing
    txPacket.startByte1 = SYNC_BYTE_1;
    txPacket.startByte2 = SYNC_BYTE_2;
    txPacket.packetSequence = sequenceCounter++; //use stored value, and increment afterwards

    // 3. Fill payload data
    txPacket.uptimeMillis = now; // uptime in milliseconds

    // Tenths of a degree, 0..3599 -- this is the resolution the wire carries,
    // and what the slave hands straight to ClockFace::prepareFrame(). Whole
    // degrees weren't enough: at these speeds a hand can move less than 1
    // degree between packets, which made the motion step unevenly instead
    // of gliding at constant speed.
    txPacket.angle1DeciDeg = uint16_t(angle1Milli / 100);
    txPacket.angle2DeciDeg = uint16_t(angle2Milli / 100);

    // fill ClockDataPacket.statusMessage with a null-terminated string by using strncpy to avoid buffer overflow
    strncpy(txPacket.statusMessage, "Message", sizeof(txPacket.statusMessage) - 1);
    txPacket.statusMessage[sizeof(txPacket.statusMessage) - 1] = '\0'; // Ensure null-termination

    // 4. Calculate the XOR checksum over everything EXCEPT the checksum byte itself
    size_t bytesToCalculate = sizeof(ClockDataPacket) - 1; // Exclude the checksum byte
    txPacket.xorChecksum = calculateChecksum((uint8_t*)&txPacket, bytesToCalculate);

    // 5. Transmit the entire struct as raw bytes over RS485
    Serial1.write((uint8_t*)&txPacket, sizeof(ClockDataPacket));
    
    // Optional: Force the hardware to finish sending before looping
    Serial1.flush();

  }
}

