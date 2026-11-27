/*
  Minimal GC9A01 driver on ESP-IDF's esp_lcd SPI layer.

  The panel initialisation sequence below is taken from Arduino_GFX
  (https://github.com/moononournation/Arduino_GFX), which is distributed under
  this licence:

    Software License Agreement (BSD License)
    Copyright (c) 2012 Adafruit Industries.  All rights reserved.
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    - Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    - Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/
#ifdef ARDUINO
#include <Arduino.h>
#include "gc9a01.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"

static esp_lcd_panel_io_handle_t io = nullptr;
static gc9a01_done_cb_t s_done;
static void *s_ctx;

static bool IRAM_ATTR on_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *,
                              void *) {
  if (s_done) s_done(s_ctx);
  return false;
}

/* Command, data length, data... ; length 0xFF = delay (next byte * 10 ms); 0 cmd ends. */
static const uint8_t INIT[] = {
  0xEF, 0,
  0xEB, 1, 0x14,
  0xFE, 0,
  0xEF, 0,
  0xEB, 1, 0x14,
  0x84, 1, 0x40,
  0x85, 1, 0xFF,
  0x86, 1, 0xFF,
  0x87, 1, 0xFF,
  0x88, 1, 0x0A,
  0x89, 1, 0x21,
  0x8A, 1, 0x00,
  0x8B, 1, 0x80,
  0x8C, 1, 0x01,
  0x8D, 1, 0x01,
  0x8E, 1, 0xFF,
  0x8F, 1, 0xFF,
  0xB6, 2, 0x00, 0x20,
  0x3A, 1, 0x05,                          /* 16-bit RGB565 */
  0x90, 4, 0x08, 0x08, 0x08, 0x08,
  0xBD, 1, 0x06,
  0xBC, 1, 0x00,
  0xFF, 3, 0x60, 0x01, 0x04,
  0xC3, 1, 0x13,
  0xC4, 1, 0x13,
  0xC9, 1, 0x22,
  0xBE, 1, 0x11,
  0xE1, 2, 0x10, 0x0E,
  0xDF, 3, 0x21, 0x0C, 0x02,
  0xF0, 6, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A,
  0xF1, 6, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F,
  0xF2, 6, 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A,
  0xF3, 6, 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F,
  0xED, 2, 0x1B, 0x0B,
  0xAE, 1, 0x77,
  0xCD, 1, 0x63,
  0x70, 9, 0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03,
  0xE8, 1, 0x34,
  0x62, 12, 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70,
  0x63, 12, 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70,
  0x64, 7, 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07,
  0x66, 10, 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00,
  0x67, 10, 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98,
  0x74, 7, 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00,
  0x98, 2, 0x3E, 0x07,
  0x35, 0,                                /* tearing effect line on */
  0x36, 1, 0x08,                          /* MADCTL: rotation 0, BGR */
  0x21, 0,                                /* inversion on (IPS panels) */
  0x11, 0xFF, 12,                         /* sleep out, wait 120 ms */
  0x29, 0xFF, 2,                          /* display on, wait 20 ms */
  0x00
};

bool gc9a01_init(int sck, int mosi, int cs, int dc, int rst, uint32_t spi_hz,
                 gc9a01_done_cb_t done_cb, void *ctx) {
  s_done = done_cb;
  s_ctx = ctx;

  if (rst >= 0) {
    pinMode(rst, OUTPUT);
    digitalWrite(rst, HIGH); delay(10);
    digitalWrite(rst, LOW);  delay(20);
    digitalWrite(rst, HIGH); delay(200);
  }

  spi_bus_config_t bus = {};
  bus.sclk_io_num = sck;
  bus.mosi_io_num = mosi;
  bus.miso_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 240 * 40 * 2;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  esp_lcd_panel_io_spi_config_t cfg = {};
  cfg.cs_gpio_num = (gpio_num_t)cs;
  cfg.dc_gpio_num = (gpio_num_t)dc;
  cfg.spi_mode = 0;
  cfg.pclk_hz = spi_hz;
  cfg.trans_queue_depth = 4;
  cfg.on_color_trans_done = on_done;
  cfg.lcd_cmd_bits = 8;
  cfg.lcd_param_bits = 8;
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &cfg, &io) != ESP_OK)
    return false;

  for (const uint8_t *p = INIT; *p; ) {
    uint8_t cmd = *p++, len = *p++;
    if (len == 0xFF) {
      esp_lcd_panel_io_tx_param(io, cmd, nullptr, 0);
      delay(*p++ * 10);
    } else {
      esp_lcd_panel_io_tx_param(io, cmd, len ? p : nullptr, len);
      p += len;
    }
  }
  return true;
}

void gc9a01_draw(int x1, int y1, int x2, int y2, const void *pixels) {
  if (!io) { if (s_done) s_done(s_ctx); return; }   /* init failed: never stall LVGL */
  uint8_t ca[4] = {(uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2};
  uint8_t ra[4] = {(uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2};
  esp_lcd_panel_io_tx_param(io, 0x2A, ca, 4);   /* column address */
  esp_lcd_panel_io_tx_param(io, 0x2B, ra, 4);   /* row address */
  esp_lcd_panel_io_tx_color(io, 0x2C, pixels, (size_t)(x2 - x1 + 1) * (y2 - y1 + 1) * 2);
}
#endif
