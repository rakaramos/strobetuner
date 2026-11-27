/*
  Desktop simulator for the strobe tuner UI.

    ./strobetuner_sim             open a 240x240 round window (2x zoom)
    ./strobetuner_sim --shots DIR write every screen, in every theme, as .bmp

  Controls (stand-ins for the rotary encoder):
    Mouse wheel / Right, Up / Left, Down   rotate
    Wheel click / Enter                    press
    Hold wheel click / Esc / Backspace     long press
*/
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "ui/ui.h"

#define W 240
#define H 240

static tuner_settings_t settings;

/* Device would retune the strobe and save to flash; here we just log it. */
void platform_settings_changed(const tuner_settings_t *s) {
  int m = tuner_target_midi(s);
  printf("[strobe] %s string %d: %s%d %.3f Hz  %s  | A4 %.1f  flash %d us  trim %+.1f ppm  "
         "backlight %d%%  theme %d  ref tone %s\n",
         tuner_tuning_name(s->tuning), TUNER_STRINGS - s->string_idx,
         tuner_note_letter(m), tuner_note_octave(m), tuner_target_hz(s),
         s->strobe_on ? "on " : "off", s->a4_tenths / 10.0, s->flash_us,
         s->trim_tenths_ppm / 10.0, s->brightness, s->theme, s->ref_tone ? "on" : "off");
  fflush(stdout);
}

/* ------------------------------------------------------------------------ */
/*  Screenshot mode                                                         */
/* ------------------------------------------------------------------------ */

static uint32_t fake_ms;
static uint32_t fake_tick(void) { return fake_ms; }
static uint32_t fb[W * H];
static uint8_t draw_buf[W * H * 4];

static void shot_flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) {
  int w = lv_area_get_width(a);
  for (int y = a->y1; y <= a->y2; y++)
    memcpy(&fb[y * W + a->x1], px + (size_t)(y - a->y1) * w * 4, (size_t)w * 4);
  lv_display_flush_ready(d);
}

static void advance(int ms) {
  for (int t = 0; t < ms; t += 10) { fake_ms += 10; lv_timer_handler(); }
}

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

/* 24-bit BMP with everything outside the round glass painted dark grey. */
static void save_bmp(const char *dir, const char *name) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(NULL);
  char path[512];
  snprintf(path, sizeof path, "%s/%s.bmp", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) { perror(path); return; }
  int row = W * 3, size = row * H;
  fputc('B', f); fputc('M', f); put32(f, 54 + size); put32(f, 0); put32(f, 54);
  put32(f, 40); put32(f, W); put32(f, H); put16(f, 1); put16(f, 24);
  put32(f, 0); put32(f, size); put32(f, 2835); put32(f, 2835); put32(f, 0); put32(f, 0);
  for (int y = H - 1; y >= 0; y--)
    for (int x = 0; x < W; x++) {
      float dx = x + 0.5f - W / 2.0f, dy = y + 0.5f - H / 2.0f;
      uint32_t c = (dx * dx + dy * dy > 120.0f * 120.0f) ? 0x202020 : fb[y * W + x];
      fputc(c & 0xFF, f); fputc((c >> 8) & 0xFF, f); fputc((c >> 16) & 0xFF, f);
    }
  fclose(f);
  printf("wrote %s\n", path);
}

static void mem_report(const char *when) {
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  printf("[mem] %-14s used %u of %u bytes (%u%%), biggest free block %u\n", when,
         (unsigned)(m.total_size - m.free_size), (unsigned)m.total_size, m.used_pct,
         (unsigned)m.free_biggest_size);
}

static void key(ui_key_t k) { ui_key(k); advance(400); }

static int run_shots(const char *dir) {
  mkdir(dir, 0755);
  lv_tick_set_cb(fake_tick);
  lv_display_t *d = lv_display_create(W, H);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(d, draw_buf, NULL, sizeof draw_buf, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d, shot_flush);

  for (int th = 0; th < ui_theme_count(); th++) {
    tuner_defaults(&settings);
    settings.theme = th;
    ui_init(&settings);
    advance(400);
    mem_report("after init");
    char n[64];
#define SHOT(label) do { snprintf(n, sizeof n, "t%d_%s", th, label); save_bmp(dir, n); } while (0)
    SHOT("01_tune");
    key(UI_KEY_CW); key(UI_KEY_CW);                          SHOT("02_tune_string4");
    key(UI_KEY_LONG_PRESS);                                 SHOT("03_strobe_off");
    key(UI_KEY_LONG_PRESS);
    key(UI_KEY_PRESS); key(UI_KEY_CW);                       SHOT("04_list_dropd");
    key(UI_KEY_CW); key(UI_KEY_CW); key(UI_KEY_CW);          SHOT("05_list_open");
    key(UI_KEY_LONG_PRESS);                                 SHOT("06_open_editor");
    key(UI_KEY_PRESS); key(UI_KEY_CCW); key(UI_KEY_CCW);     SHOT("07_open_editing");
    key(UI_KEY_PRESS); key(UI_KEY_CCW);                      SHOT("08_open_done");
    key(UI_KEY_CCW);                                         SHOT("09_open_reset");
    key(UI_KEY_CW); key(UI_KEY_PRESS);                       SHOT("10_tune_open");
    key(UI_KEY_PRESS); key(UI_KEY_CW);                       SHOT("11_list_settings");
    key(UI_KEY_PRESS);                                       SHOT("12_menu");
    for (int i = 0; i < 5; i++) key(UI_KEY_CW);
    key(UI_KEY_PRESS); key(UI_KEY_CW);                       SHOT("13_edit_reftone");
    mem_report("after flow");
  }
  return 0;
}

/* ------------------------------------------------------------------------ */
/*  Interactive window                                                      */
/* ------------------------------------------------------------------------ */
#if LV_USE_SDL
#include <SDL2/SDL.h>

static bool long_sent;

/* An invisible slider receives keyboard + wheel input via an LVGL group and
   turns it into the four encoder keys the UI understands. (A slider is used
   only because LVGL lets editable widgets receive wheel rotation.) */
static void input_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *in = lv_indev_active();
  bool wheel = in && lv_indev_get_type(in) == LV_INDEV_TYPE_ENCODER;

  if (code == LV_EVENT_LONG_PRESSED && wheel) {
    long_sent = true;
    ui_key(UI_KEY_LONG_PRESS);
  } else if (code == LV_EVENT_KEY) {
    uint32_t k = lv_event_get_key(e);
    if (k == LV_KEY_RIGHT || k == LV_KEY_UP) ui_key(UI_KEY_CW);
    else if (k == LV_KEY_LEFT || k == LV_KEY_DOWN) ui_key(UI_KEY_CCW);
    else if (k == LV_KEY_ESC || k == LV_KEY_BACKSPACE) ui_key(UI_KEY_LONG_PRESS);
    else if (k == LV_KEY_ENTER) {
      if (long_sent) long_sent = false;   /* release after a long press */
      else ui_key(UI_KEY_PRESS);
    }
  }
}

static int run_window(void) {
  lv_display_t *d = lv_sdl_window_create(W, H);
  lv_sdl_window_set_zoom(d, 2);
  lv_sdl_window_set_title(d, "Strobe tuner (GC9A01 240x240)");

  lv_group_t *g = lv_group_create();
  lv_indev_set_group(lv_sdl_keyboard_create(), g);
  lv_indev_set_group(lv_sdl_mousewheel_create(), g);

  lv_obj_t *catcher = lv_slider_create(lv_layer_sys());
  lv_obj_set_size(catcher, 1, 1);
  lv_obj_set_style_opa(catcher, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(catcher, input_cb, LV_EVENT_ALL, NULL);
  lv_group_add_obj(g, catcher);
  lv_group_set_editing(g, true);

  /* Round bezel: hides the corners the real panel doesn't have. */
  lv_obj_t *bezel = lv_obj_create(lv_layer_sys());
  lv_obj_remove_style_all(bezel);
  lv_obj_set_size(bezel, 340, 340);
  lv_obj_center(bezel);
  lv_obj_set_style_radius(bezel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(bezel, 50, 0);
  lv_obj_set_style_border_color(bezel, lv_color_hex(0x202020), 0);
  lv_obj_set_style_border_opa(bezel, LV_OPA_COVER, 0);
  lv_obj_set_clickable(bezel, false);

  ui_init(&settings);
  platform_settings_changed(&settings);
  for (;;) {
    uint32_t ms = lv_timer_handler();
    SDL_Delay(ms < 5 ? 5 : ms > 30 ? 30 : ms);
  }
  return 0;
}
#endif

int main(int argc, char **argv) {
  lv_init();
  tuner_defaults(&settings);
  if (argc == 3 && strcmp(argv[1], "--shots") == 0) return run_shots(argv[2]);
#if LV_USE_SDL
  return run_window();
#else
  fprintf(stderr, "Built without SDL; only --shots DIR is available.\n");
  return 1;
#endif
}
