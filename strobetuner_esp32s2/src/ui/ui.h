/*
  Strobe tuner UI for a 240x240 round display (GC9A01), built with LVGL 9.
  Shared by the ESP32-S2 firmware and the desktop simulator.

  Input is a rotary encoder with a push button, reduced to four keys so the
  same code works with a real encoder, a mouse wheel or the keyboard.
*/
#ifndef UI_H
#define UI_H

#include "../core/tuner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  UI_KEY_CW,          /* encoder turned clockwise      (sim: Right/Up, wheel) */
  UI_KEY_CCW,         /* encoder turned anticlockwise  (sim: Left/Down, wheel) */
  UI_KEY_PRESS,       /* short press                   (sim: Enter, wheel click) */
  UI_KEY_LONG_PRESS,  /* press and hold ~0.6 s         (sim: Esc / Backspace) */
} ui_key_t;

/* Build the screens. The UI edits *settings in place and calls
   platform_settings_changed() after every change. */
void ui_init(tuner_settings_t *settings);

/* Feed one input event. */
void ui_key(ui_key_t key);

/* Redraw after settings were changed from outside the UI (e.g. serial). */
void ui_refresh(void);

/* Number of colour themes, for range checks. */
int ui_theme_count(void);

#ifdef __cplusplus
}
#endif

#endif
