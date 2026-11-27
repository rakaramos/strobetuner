/*
  Strobe flash schedule, shared by the firmware's timer interrupt and the host
  test (simulator/strobe_test.c), so the exact code that times the LEDs can be
  checked on a computer.

  One period of the note:   t=0 LED1 on, t=flash LED1 off,
                            t=P/2 LED2 on, t=P/2+flash LED2 off, repeat.
  Times are in timer ticks (1 us) * 65536, so fractions of a microsecond
  accumulate instead of being rounded away every period.
*/
#ifndef STROBE_MATH_H
#define STROBE_MATH_H

#include <stdint.h>
#include <math.h>

typedef struct {
  uint64_t period_q16, half_q16, flash_q16;
} strobe_plan_t;

typedef struct {
  uint64_t base_q16;   /* start of the current period */
  uint8_t state;       /* 0 LED1 on, 1 LED1 off, 2 LED2 on, 3 LED2 off */
} strobe_state_t;

/* trim_ppm > 0 means the board's crystal runs fast, so the period is lengthened. */
static inline strobe_plan_t strobe_plan(double hz, double flash_us, double trim_ppm) {
  strobe_plan_t p;
  double period_us = 1e6 / hz * (1.0 + trim_ppm * 1e-6);
  if (flash_us > period_us / 4) flash_us = period_us / 4;   /* keep flashes apart */
  p.period_q16 = (uint64_t)llround(period_us * 65536.0);
  p.half_q16 = p.period_q16 / 2;
  p.flash_q16 = (uint64_t)llround(flash_us * 65536.0);
  return p;
}

/* Do the current step: sets *led (0 or 1) and *level (1 on, 0 off) to drive now,
   advances the state, and returns the tick at which to call it again. */
static inline uint64_t strobe_step(strobe_state_t *s, const strobe_plan_t *p,
                                   int *led, int *level) {
  uint64_t next;
  switch (s->state) {
    case 0: *led = 0; *level = 1; next = s->base_q16 + p->flash_q16; break;
    case 1: *led = 0; *level = 0; next = s->base_q16 + p->half_q16; break;
    case 2: *led = 1; *level = 1; next = s->base_q16 + p->half_q16 + p->flash_q16; break;
    default:
      *led = 1; *level = 0;
      s->base_q16 += p->period_q16;   /* advance by the exact period: no drift */
      next = s->base_q16;
      break;
  }
  s->state = (uint8_t)((s->state + 1) & 3);
  return next >> 16;
}

#endif
