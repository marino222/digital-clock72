// Phase 5.2b: disposable test master for slice 1 (auto-addressing).
//
// Sends what the slaves need to show their address screen:
//   ADDR   TRUNK(row 0, layout)          every 100 ms, to ADDR_IN of node 0
//   RS485  CMD_DISPLAY, CMD_DIAGNOSTIC   every 5 s, broadcast
//
// Serial commands (115200, end with Enter). Each one gets one reply line.
//   l 2 2    layout: nodes per row, one number per row
//   d 1      diagnostic mode: 1 = show address, 0 = normal
//
// Wiring (ESP32 -> breakout / node 0):
//   GPIO17 (UART2 TX) -> DI      GPIO16 (UART2 RX) <- RO
//   GPIO4             -> DE and RE, held HIGH: always transmitting
//   GPIO25 (UART1 TX) -> ADDR_IN of node 0
//   GND shared
#include <Arduino.h>
#include "protocol.h"   // software/shared, via -I in platformio.ini

// ---- Pins ----
constexpr int PIN_RS485_TX = 17;
constexpr int PIN_RS485_RX = 16;
constexpr int PIN_RS485_DE = 4;
constexpr int PIN_ADDR_TX  = 25;

// ---- What is sent ----
constexpr uint32_t REPEAT_MS = 5000;   // RS485 repeat, as the real master will do

protocol::AddrTrunkData trunk   = {0, 2, {2, 2}};   // row 0, 2 rows, [2, 2]
protocol::DisplayData   display = {1, 179};         // 90 degrees, 70 %
uint8_t                 diagMode = protocol::DIAG_SHOW_ADDRESS;

uint8_t  sequence   = 0;       // RS485 frame counter, rolls over at 255
uint32_t lastAddrMs = 0;
uint32_t lastBusMs  = 0;

// ---- Sending ----

void sendTrunk() {
    uint8_t buf[protocol::MAX_ADDR_ENCODED_FRAME];
    const size_t len = protocol::buildAddrTrunk(trunk, buf, sizeof(buf));
    Serial1.write(buf, len);
}

void sendBus(protocol::CommandType cmd, const void* payload, size_t payloadLen) {
    uint8_t buf[protocol::MAX_ENCODED_FRAME];
    const size_t len = protocol::buildFrame(cmd, protocol::GLOBAL_BROADCAST_ID, sequence++,
                                            payload, payloadLen, buf, sizeof(buf));
    Serial2.write(buf, len);
}

void sendBusState() {
    sendBus(protocol::CMD_DISPLAY,    &display,  sizeof(display));
    sendBus(protocol::CMD_DIAGNOSTIC, &diagMode, sizeof(diagMode));
}

// ---- Serial commands ----

// "l 2 2": reads the numbers after 'l' into the layout.
void setLayout(char* args) {
    protocol::AddrTrunkData next = {0, 0, {}};
    for (char* tok = strtok(args, " "); tok != nullptr; tok = strtok(nullptr, " ")) {
        if (next.rowCount == protocol::MAX_ADDR_ROWS) break;
        next.rowLengths[next.rowCount++] = uint8_t(atoi(tok));
    }
    if (next.rowCount == 0) {
        Serial.println("layout: give at least one row, e.g. l 2 2");
        return;
    }
    trunk = next;
    sendTrunk();                       // on change: at once, then every 100 ms

    Serial.print("layout:");
    for (uint8_t i = 0; i < trunk.rowCount; i++) {
        Serial.print(' ');
        Serial.print(trunk.rowLengths[i]);
    }
    Serial.println();
}

// "d 1": sets the diagnostic mode.
void setDiag(char* args) {
    diagMode = uint8_t(atoi(args));
    sendBusState();                    // on change: at once, then every 5 s
    Serial.printf("diag: %u\n", diagMode);
}

void handleLine(char* line) {
    switch (line[0]) {
        case 'l': setLayout(line + 1); break;
        case 'd': setDiag(line + 1);   break;
        default:  Serial.println("commands: l <nodes per row> ..., d <0|1>"); break;
    }
}

// Collects characters until Enter. Never waits.
void readCommands() {
    static char line[200];
    static size_t len = 0;

    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                line[len] = '\0';
                handleLine(line);
            }
            len = 0;
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        }
    }
}

// ---- Arduino ----

void setup() {
    Serial.begin(115200);              // USB: commands only, no output on its own

    pinMode(PIN_RS485_DE, OUTPUT);
    digitalWrite(PIN_RS485_DE, HIGH);  // DE on, RE off: the test master only sends
    Serial2.begin(protocol::RS485_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);

    Serial1.begin(protocol::ADDR_BAUD, SERIAL_8N1, -1, PIN_ADDR_TX);   // TX only

    sendTrunk();
    sendBusState();
}

void loop() {
    readCommands();

    const uint32_t now = millis();
    if (now - lastAddrMs >= protocol::ADDR_REPEAT_MS) {
        lastAddrMs = now;
        sendTrunk();
    }
    if (now - lastBusMs >= REPEAT_MS) {
        lastBusMs = now;
        sendBusState();
    }
}
