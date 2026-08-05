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

// Serial Communication Speed
const long RS485_BAUD_RATE = 1000000;