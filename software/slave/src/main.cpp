// Startup and the two loops. Only connects the modules.
// See software/slave/README.md for the architecture.
#include <Arduino.h>
#include <hardware/clocks.h>   // clock_configure

#include "mailbox.h"
#include "address.h"
#include "bus_rx.h"
#include "backlight.h"
#include "renderer.h"

// Startup handshake (from phase 3.2). arduino-pico starts core 1 before core 0
// enters setup(). Core 1 must wait until core 0 has set the SPI clock and the
// mailbox lock, or the display would come up on the 48 MHz USB clock.
static volatile bool coreZeroReady = false;

// ---- Core 0: RS485, ADDR chain ----

void setup() {
    // Peripheral clock (SPI, UART) from the 133 MHz system clock instead of 48 MHz USB.
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    clock_get_hz(clk_sys), clock_get_hz(clk_sys));
    mailbox::init();

    __sync_synchronize();      // make the two lines above visible to core 1 before the flag
    coreZeroReady = true;      // releases core 1

    address::begin();
    bus_rx::begin();
}

void loop() {
    address::poll();
    bus_rx::poll();
}

// ---- Core 1: display ----

void setup1() {
    while (!coreZeroReady) {}  // see the startup handshake above

    backlight::begin();
    renderer::begin();
}

void loop1() {
    renderer::draw();
}
