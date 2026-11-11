#pragma once

// ---------------------------------------------------------
// RS485 Hardware Configuration
// ---------------------------------------------------------

// UART0 Pins
const int RS485_TX_PIN = 12;   // Connects to DI (Data In) on the RS485 module
const int RS485_RX_PIN = 13;   // Connects to RO (Receive Out) on the RS485 module

// Transceiver Direction Control Pin
const int RS485_CTRL_PIN = 15; // Connects to BOTH DE and RE on the RS485 module
                              // HIGH = Transmit Mode
                              // LOW  = Receive Mode

// Node PCB v1.0: DE and /RE are separate there. GPIO15 is /RE only, DE is on
// GPIO14 (10 kOhm pull-down). Not connected on the breadboard setup.
const int RS485_DE_PIN = 14;   // HIGH = driver on

// Serial Communication Speed
const long RS485_BAUD_RATE = 1000000;