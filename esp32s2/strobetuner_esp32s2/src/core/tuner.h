/*
  Tuner model: settings, tunings and note maths.
  Plain C, no LVGL or Arduino, so it builds for the ESP32-S2, the desktop
  simulator and the host tests alike.

  Tunings: four fixed ones plus "Open", a custom tuning the user sets string by
  string. Open starts out as Standard.
*/
#ifndef TUNER_H
#define TUNER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TUNER_STRINGS 6          /* index 0 = string 6 (low E) ... 5 = string 1 */
#define TUNER_OPEN_RANGE 7       /* Open strings may move +/- this many semitones */

enum {
  TUNING_STANDARD,
  TUNING_DROP_D,
  TUNING_HALF_DOWN,
  TUNING_FULL_DOWN,
  TUNING_OPEN,
  TUNING_COUNT
};

/* Everything the user can change. Saved on the device and restored at power-up,
   so the tuner comes back on the last tuning and string used. */
typedef struct {
  int tuning;                    /* TUNING_* */
  int string_idx;                /* 0 = string 6 (lowest) ... 5 = string 1 */
  int open_midi[TUNER_STRINGS];  /* the user's Open tuning, MIDI note numbers */
  int a4_tenths;                 /* reference pitch in 0.1 Hz, 4400 = 440.0 Hz */
  int flash_us;                  /* LED on-time per flash */
  int trim_tenths_ppm;           /* crystal correction, 0.1 ppm (+ = board runs fast) */
  int brightness;                /* display backlight, 10..100 % */
  int theme;                     /* UI colour scheme, index into the UI's theme list */
  int ref_tone;                  /* 1 = reference square wave on the tone pin */
  bool strobe_on;
} tuner_settings_t;

void tuner_defaults(tuner_settings_t *s);
/* Clamp anything out of range (e.g. after loading from flash). */
void tuner_sanitize(tuner_settings_t *s);

const char *tuner_tuning_name(int tuning);
/* The six notes of a tuning, low string first (Open comes from settings). */
void tuner_tuning_notes(const tuner_settings_t *s, int tuning, int out[TUNER_STRINGS]);
/* Standard tuning's note for a string index, the reference for Open. */
int tuner_standard_midi(int string_idx);
/* True once the user has changed any Open string. */
bool tuner_open_is_set(const tuner_settings_t *s);
/* Put the Open tuning back to Standard. */
void tuner_open_reset(tuner_settings_t *s);
/* Raise/lower one Open string by dir semitones, within +/- TUNER_OPEN_RANGE. */
void tuner_open_adjust(tuner_settings_t *s, int string_idx, int dir);

/* The note currently being tuned. */
int tuner_target_midi(const tuner_settings_t *s);
double tuner_target_hz(const tuner_settings_t *s);
double tuner_midi_to_hz(const tuner_settings_t *s, int midi);

/* Next/previous string, wrapping. */
void tuner_step(tuner_settings_t *s, int dir);

/* "E", "F#" ... and octave number for a MIDI note. */
const char *tuner_note_letter(int midi);
int tuner_note_octave(int midi);

/* ---- Platform hook: implemented by the device sketch and the simulator ---- */
/* Called by the UI whenever a setting changes. Apply it (retune the strobe,
   backlight, tone) and persist it when convenient. */
void platform_settings_changed(const tuner_settings_t *s);

#ifdef __cplusplus
}
#endif

#endif
