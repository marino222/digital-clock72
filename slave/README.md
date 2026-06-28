# Slave_Firmware


This will hold the firmware for a single pixel node, one RP2040 driving one round GC9A01 LCD. It needs to listen on RS485, decode whatever broadcast protocol the master controller sends, auto-address itself in the daisy chain, and render the clock face (and later patterns/Snake) at 60 FPS.

See the root [README.md](../README.md) for the full picture.
