#pragma once
#include <LovyanGFX.hpp>
#include "board.h"

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
      cfg.freq_write = 70'000'000; // 66.5MHz, GC9A01 can usually handle this
      cfg.freq_read  = 16000000;

      // pins from board.h
      cfg.pin_sclk = PIN_DISP_SCK;         
      cfg.pin_mosi = PIN_DISP_MOSI;
      cfg.pin_miso = -1;  // not wired, write-only         
      cfg.pin_dc   = PIN_DISP_DC;

      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // Panel config
      auto cfg = _panel_instance.config();

      cfg.pin_cs   = PIN_DISP_CS;
      cfg.pin_rst  = PIN_DISP_RST;
      cfg.pin_busy = -1;

      //Display resolution
      cfg.panel_width  = 240;
      cfg.panel_height = 240;
      
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable   = false;
      cfg.invert     = true;   
      cfg.rgb_order  = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};