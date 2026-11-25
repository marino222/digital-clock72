# Master

This will hold the master firmware: a C++ PlatformIO project running on an ESP32-S3. The master keeps time, runs the animation engine, works out what the whole array should show and broadcasts that state to all nodes over RS485. It also hosts a small web UI for controlling and configuring the clock.

The base principles both sides follow are in the [software README](../README.md). The detailed master architecture will be documented here. See the root [README.md](../../README.md) for the full picture.
