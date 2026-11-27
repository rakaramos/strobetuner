#include "tuner.h"
#include <math.h>

/* MIDI numbers: C4 = 60, A4 = 69, E2 = 40. Low string first. */
static const char *NAMES[TUNING_COUNT] = {
  "Standard", "Drop D", "Half step down", "Full step down", "Open",
};
static const int FIXED[TUNING_OPEN][TUNER_STRINGS] = {
  {40, 45, 50, 55, 59, 64},   /* E2 A2 D3 G3 B3 E4 */
  {38, 45, 50, 55, 59, 64},   /* D2 A2 D3 G3 B3 E4 */
  {39, 44, 49, 54, 58, 63},   /* Eb Ab Db Gb Bb Eb */
  {38, 43, 48, 53, 57, 62},   /* D2 G2 C3 F3 A3 D4 */
};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

const char *tuner_tuning_name(int t) { return NAMES[clampi(t, 0, TUNING_COUNT - 1)]; }
int tuner_standard_midi(int i) { return FIXED[TUNING_STANDARD][clampi(i, 0, TUNER_STRINGS - 1)]; }

void tuner_tuning_notes(const tuner_settings_t *s, int t, int out[TUNER_STRINGS]) {
  for (int i = 0; i < TUNER_STRINGS; i++)
    out[i] = t == TUNING_OPEN ? s->open_midi[i] : FIXED[clampi(t, 0, TUNING_OPEN - 1)][i];
}

bool tuner_open_is_set(const tuner_settings_t *s) {
  for (int i = 0; i < TUNER_STRINGS; i++)
    if (s->open_midi[i] != tuner_standard_midi(i)) return true;
  return false;
}

void tuner_open_reset(tuner_settings_t *s) {
  for (int i = 0; i < TUNER_STRINGS; i++) s->open_midi[i] = tuner_standard_midi(i);
}

void tuner_open_adjust(tuner_settings_t *s, int i, int dir) {
  if (i < 0 || i >= TUNER_STRINGS) return;
  int base = tuner_standard_midi(i);
  s->open_midi[i] = clampi(s->open_midi[i] + dir, base - TUNER_OPEN_RANGE, base + TUNER_OPEN_RANGE);
}

void tuner_defaults(tuner_settings_t *s) {
  s->tuning = TUNING_STANDARD;
  s->string_idx = 0;
  tuner_open_reset(s);
  s->a4_tenths = 4400;
  s->flash_us = 250;
  s->trim_tenths_ppm = 0;
  s->brightness = 80;
  s->theme = 0;
  s->ref_tone = 0;
  s->strobe_on = true;
}

void tuner_sanitize(tuner_settings_t *s) {
  s->tuning = clampi(s->tuning, 0, TUNING_COUNT - 1);
  s->string_idx = clampi(s->string_idx, 0, TUNER_STRINGS - 1);
  for (int i = 0; i < TUNER_STRINGS; i++) {
    int base = tuner_standard_midi(i);
    s->open_midi[i] = clampi(s->open_midi[i], base - TUNER_OPEN_RANGE, base + TUNER_OPEN_RANGE);
  }
  s->a4_tenths = clampi(s->a4_tenths, 4000, 4800);
  s->flash_us = clampi(s->flash_us, 50, 1000);
  s->trim_tenths_ppm = clampi(s->trim_tenths_ppm, -2000, 2000);
  s->brightness = clampi(s->brightness, 10, 100);
  if (s->theme < 0) s->theme = 0;   /* upper bound is checked by the UI */
  s->ref_tone = s->ref_tone ? 1 : 0;
}

int tuner_target_midi(const tuner_settings_t *s) {
  int n[TUNER_STRINGS];
  tuner_tuning_notes(s, s->tuning, n);
  return n[clampi(s->string_idx, 0, TUNER_STRINGS - 1)];
}

double tuner_midi_to_hz(const tuner_settings_t *s, int midi) {
  return (s->a4_tenths / 10.0) * pow(2.0, (midi - 69) / 12.0);
}

double tuner_target_hz(const tuner_settings_t *s) {
  return tuner_midi_to_hz(s, tuner_target_midi(s));
}

void tuner_step(tuner_settings_t *s, int dir) {
  s->string_idx = ((s->string_idx + dir) % TUNER_STRINGS + TUNER_STRINGS) % TUNER_STRINGS;
}

static const char *LETTERS[] = {"C", "C#", "D", "D#", "E", "F",
                                "F#", "G", "G#", "A", "A#", "B"};

const char *tuner_note_letter(int midi) { return LETTERS[((midi % 12) + 12) % 12]; }
int tuner_note_octave(int midi) { return (int)floor(midi / 12.0) - 1; }
