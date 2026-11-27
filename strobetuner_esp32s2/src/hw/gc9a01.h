/*
  Minimal GC9A01 (240x240 round IPS) driver on ESP-IDF's esp_lcd SPI layer.
  DMA transfers; done_cb is called from an interrupt when a draw() finishes.
  Device-only: not used by the simulator.
*/
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef void (*gc9a01_done_cb_t)(void *ctx);

bool gc9a01_init(int sck, int mosi, int cs, int dc, int rst, uint32_t spi_hz,
                 gc9a01_done_cb_t done_cb, void *ctx);

/* Send a block of big-endian RGB565 pixels to x1..x2, y1..y2 (inclusive).
   Returns at once; the buffer must stay untouched until done_cb runs. */
void gc9a01_draw(int x1, int y1, int x2, int y2, const void *pixels);
