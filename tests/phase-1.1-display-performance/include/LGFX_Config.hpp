#pragma once
#include <LovyanGFX.hpp>
#include "board_pins.h"

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_GC9A01     _panel_instance;
  lgfx::Bus_SPI          _bus_instance;

public:
  LGFX(void)
  {
    { // SPI bus config
      auto cfg = _bus_instance.config();

      cfg.spi_host   = 0;        // PICO_DEFAULT_SPI or which SPI peripheral (0 or 1)
      cfg.spi_mode   = 0;
      cfg.freq_write = SPI_WRITE_HZ; // 40MHz, GC9A01 can usually handle this
      cfg.freq_read  = 16000000;

      cfg.pin_sclk = PIN_SCLK;         // adjust to your wiring
      cfg.pin_mosi = PIN_MOSI;
      cfg.pin_miso = PIN_MISO;         // not used, GC9A01 is write-only
      cfg.pin_dc   = PIN_DC;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel config
      auto cfg = _panel_instance.config();

      cfg.pin_cs   = PIN_CS;
      cfg.pin_rst  = PIN_RST;
      cfg.pin_busy = -1;

      cfg.panel_width  = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable   = false;
      cfg.invert     = true;    // GC9A01 usually needs inverted colors
      cfg.rgb_order  = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};