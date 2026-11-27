/*
  Host test for the strobe timing. Runs the exact schedule the ESP32's timer
  interrupt uses (src/core/strobe_math.h) and checks the LED edges it produces.

    cmake --build build && ./build/strobe_test

  Checks, for every note A0..C7 and every string of every tuning (incl. Open):
    - note maths matches reference equal-temperament frequencies
    - the long-run flash rate equals the note frequency (< 0.001 cents)
    - every single period is within 1 us (one timer tick) of ideal
    - green flashes half a period after red, each flash lasts flash_us
    - alarm times always move forward (the timer can never miss one)
    - clock trim shifts the period by exactly the requested ppm
*/
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "core/tuner.h"
#include "core/strobe_math.h"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

typedef struct { double cents, max_jitter_us, phase_err_us, flash_err_us; long flashes; } result_t;

/* Run the schedule for `periods` periods and measure what the LED pins would do. */
static result_t run(double hz, double flash_us, double trim_ppm, long periods) {
  strobe_plan_t plan = strobe_plan(hz, flash_us, trim_ppm);
  strobe_state_t st = {(uint64_t)100 << 16, 0};
  double ideal = 1e6 / hz * (1.0 + trim_ppm * 1e-6);
  double want_flash = flash_us < ideal / 4 ? flash_us : ideal / 4;
  uint64_t now = 100, first = 0, last = 0, rise1 = 0, rise[2] = {0, 0};
  result_t r = {0, 0, 0, 0, 0};

  for (long i = 0; i < periods * 4; i++) {
    int led, level;
    uint64_t next = strobe_step(&st, &plan, &led, &level);   /* edge happens at `now` */
    if (level) {
      if (led == 0) {
        if (r.flashes == 0) first = now;
        else {
          double j = fabs((double)(now - last) - ideal);
          if (j > r.max_jitter_us) r.max_jitter_us = j;
        }
        last = now; r.flashes++; rise1 = now;
      } else {
        double e = fabs((double)(now - rise1) - ideal / 2);
        if (e > r.phase_err_us) r.phase_err_us = e;
      }
      rise[led] = now;
    } else {
      double e = fabs((double)(now - rise[led]) - want_flash);
      if (e > r.flash_err_us) r.flash_err_us = e;
    }
    CHECK(next > now, "alarm went backwards at %.3f Hz", hz);
    now = next;
  }
  double measured = (double)(last - first) / (r.flashes - 1);
  r.cents = 1200.0 * log2(ideal / measured);
  return r;
}

int main(void) {
  tuner_settings_t s;
  tuner_defaults(&s);

  /* 1. Note maths against reference values (A4 = 440 Hz, 12-TET). */
  struct { int midi; double hz; } ref[] = {
    {40, 82.4069}, {45, 110.0000}, {50, 146.8324}, {55, 195.9977},
    {59, 246.9417}, {64, 329.6276}, {38, 73.4162}, {28, 41.2034}, {69, 440.0},
  };
  for (unsigned i = 0; i < sizeof ref / sizeof ref[0]; i++) {
    double hz = tuner_midi_to_hz(&s, ref[i].midi);
    CHECK(fabs(hz - ref[i].hz) < 0.0005, "MIDI %d = %.4f Hz, want %.4f", ref[i].midi, hz, ref[i].hz);
  }

  /* 2. Every note on the piano range, 20000 periods each. */
  double worst_cents = 0, worst_jit = 0, worst_phase = 0, worst_flash = 0;
  for (int m = 21; m <= 96; m++) {
    double hz = tuner_midi_to_hz(&s, m);
    result_t r = run(hz, 250, 0, 20000);
    if (fabs(r.cents) > worst_cents) worst_cents = fabs(r.cents);
    if (r.max_jitter_us > worst_jit) worst_jit = r.max_jitter_us;
    if (r.phase_err_us > worst_phase) worst_phase = r.phase_err_us;
    if (r.flash_err_us > worst_flash) worst_flash = r.flash_err_us;
    CHECK(fabs(r.cents) < 0.001, "%s%d: rate off by %.5f cents", tuner_note_letter(m), tuner_note_octave(m), r.cents);
    CHECK(r.max_jitter_us <= 1.0, "%s%d: period jitter %.2f us", tuner_note_letter(m), tuner_note_octave(m), r.max_jitter_us);
    CHECK(r.phase_err_us <= 1.0, "%s%d: green phase off by %.2f us", tuner_note_letter(m), tuner_note_octave(m), r.phase_err_us);
    CHECK(r.flash_err_us <= 1.0, "%s%d: flash length off by %.2f us", tuner_note_letter(m), tuner_note_octave(m), r.flash_err_us);
  }
  printf("A0..C7 (76 notes x 20000 periods): worst rate error %.6f cents, "
         "worst single-period error %.2f us, phase %.2f us, flash %.2f us\n",
         worst_cents, worst_jit, worst_phase, worst_flash);

  /* 3. Every string of every tuning, 100000 periods each (about 20 minutes of
        flashing at E2). Open is tested at Standard and at its +/-7 semitone limits. */
  printf("\n%-16s %-6s %11s %13s %12s\n", "Tuning", "String", "Note Hz", "LED period us", "rate error");
  for (int pass = 0; pass < TUNING_COUNT + 2; pass++) {
    int t = pass < TUNING_COUNT ? pass : TUNING_OPEN;
    tuner_open_reset(&s);
    if (pass >= TUNING_COUNT)
      for (int i = 0; i < TUNER_STRINGS; i++)
        tuner_open_adjust(&s, i, pass == TUNING_COUNT ? -TUNER_OPEN_RANGE : TUNER_OPEN_RANGE);
    int notes[TUNER_STRINGS];
    tuner_tuning_notes(&s, t, notes);
    const char *label = pass == TUNING_COUNT ? "Open (all -7)" : pass > TUNING_COUNT ? "Open (all +7)"
                                                                                   : tuner_tuning_name(t);
    for (int i = 0; i < TUNER_STRINGS; i++) {
      double hz = tuner_midi_to_hz(&s, notes[i]);
      result_t r = run(hz, 250, 0, 100000);
      CHECK(fabs(r.cents) < 0.001, "%s string %d off by %.5f cents", label, TUNER_STRINGS - i, r.cents);
      CHECK(r.max_jitter_us <= 1.0, "%s string %d jitter %.2f us", label, TUNER_STRINGS - i, r.max_jitter_us);
      printf("%-16s %-6d %11.4f %13.4f %+10.6fc\n", i == 0 ? label : "", TUNER_STRINGS - i,
             hz, 1e6 / hz, r.cents);
    }
  }
  tuner_open_reset(&s);

  /* 4. Clock trim: +20 ppm must lengthen the period by exactly 20 ppm. */
  {
    double hz = 82.4069;
    strobe_plan_t a = strobe_plan(hz, 250, 0), b = strobe_plan(hz, 250, 20);
    double ppm = ((double)b.period_q16 / a.period_q16 - 1.0) * 1e6;
    CHECK(fabs(ppm - 20.0) < 0.01, "trim +20 ppm gave %.4f ppm", ppm);
    printf("\nClock trim +20 ppm -> period +%.4f ppm\n", ppm);
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
