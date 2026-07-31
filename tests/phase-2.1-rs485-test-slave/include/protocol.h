#pragma once
#include <Arduino.h>

/*
 * ---------------------------------------------------------
 * WHY WE USE 0xAA AND 0x55 FOR SYNC BYTES
 * ---------------------------------------------------------
 * These specific hex values act as our "Start of Frame" markers. 
 * They are chosen for how they look in binary on the physical wire:
 * 
 *   0xAA = 10101010
 *   0x55 = 01010101
 * 
 * 1. Hardware Clock Sync: 
 *    Standard serial (UART) does not have a dedicated clock wire. 
 *    Sending 0xAA followed by 0x55 pushes a perfectly alternating 
 *    square wave down the RS485 bus. The receiver will only start
 *    reading the packet when it sees this distinct pattern.
 * 
 * 2. Uniqueness (Framing): 
 *    This 16-bit alternating pattern is a highly distinct signature. 
 *    It is statistically unlikely to appear randomly inside our 
 *    data ensuring the receiver doesn't 
 *    accidentally start reading a packet starting from the middle 
 *    of our payload.
 */
const uint8_t SYNC_BYTE_1 = 0xAA;
const uint8_t SYNC_BYTE_2 = 0x55;


#pragma pack(push, 1) // IMPORTANT: This forces the compiler to not add any padding bytes between the struct members
struct ClockDataPacket {
    // --- Header ---
    uint8_t startByte1;      // Always SYNC_BYTE_1
    uint8_t startByte2;      // Always SYNC_BYTE_2
    uint32_t packetSequence; // Increments by 1 each time t o track dropped packets
    
    // --- Meaningful Payload ---
    uint32_t uptimeMillis;
    uint16_t angle1DeciDeg;   // Angle of the first clock hand, in tenths of a degree (0..3599)
    uint16_t angle2DeciDeg;   // Angle of the second clock hand, in tenths of a degree (0..3599)
    char statusMessage[16];  // Fixed-size C-string for short text
    
    // --- Footer ---
    uint8_t xorChecksum;     // Mathematical verification of the data
};
#pragma pack(pop)

// Simple XOR checksum function
// We pass the memory address of the data as a byte pointer, and how many bytes to read
inline uint8_t calculateChecksum(const uint8_t* data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i]; // Bitwise XOR
    }
    return checksum;
}