#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_pins.h"

// LovyanGFX device definition for the Phase 1.1 breadboard setup:
// one GC9A01 round LCD on SPI, driven write-only (no MISO wired).
// This is the only file that should need a counterpart for a different
// board revision (e.g. a new class LGFX_NodePCB_v1 with the final
// PCB's pin assignments) -- no LovyanGFX library files are edited.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;

 public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = 0;
      cfg.spi_mode = 0;
      cfg.freq_write = board::SPI_WRITE_HZ;
      cfg.freq_read = 16'000'000;
      cfg.pin_sclk = board::PIN_SCLK;
      cfg.pin_mosi = board::PIN_MOSI;
      cfg.pin_miso = board::PIN_MISO;
      cfg.pin_dc = board::PIN_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = board::PIN_CS;
      cfg.pin_rst = board::PIN_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.readable = false;  // MISO not wired
      cfg.invert = true;     // GC9A01 typically needs this, confirm visually on bring-up
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }
};
