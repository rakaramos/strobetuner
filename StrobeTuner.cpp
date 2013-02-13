#include "Arduino.h"
#include "StrobeTuner.h"

// Values for state_ are: 0: LEDs off, waiting for LED1; 1: LED1 on; 2: LEDs
// off, waiting for LED2; 3: LED2 on; 4+: not running
static const int STATE_IDLE = 4;

StrobeTuner::StrobeTuner(int led1_pin, int led2_pin) {
  pinMode(led1_pin, OUTPUT);
  pinMode(led2_pin, OUTPUT);
  digitalWrite(led1_pin, LOW);
  digitalWrite(led2_pin, LOW);

  led_pins_[0] = led1_pin;
  led_pins_[1] = led2_pin;
  state_ = STATE_IDLE;
}

void StrobeTuner::tune(unsigned int note_period, unsigned int current_micros) {
  unsigned int off_duration = (note_period >> 1) - STROBE_DURATION;

  state_ = (note_period == 0u ? STATE_IDLE : 0);
  state_durations_[0] = off_duration;
  state_durations_[1] = STROBE_DURATION;
  state_durations_[2] = note_period - off_duration - (STROBE_DURATION << 1);
  state_durations_[3] = STROBE_DURATION;
  last_state_change_time_ = current_micros;

  digitalWrite(led_pins_[0], LOW);
  digitalWrite(led_pins_[1], LOW);
}

void StrobeTuner::update(unsigned int current_micros) {
  if(state_ >= STATE_IDLE)
    return;

  // Detect when it's time to change state.  Note that the expression is
  // written kind of awkwardly here to make the arithmetic safe for when the
  // timer value wraps around.
  if(current_micros - last_state_change_time_ >= state_durations_[state_]) {
    // I apologize for how much low-level bit magic is involved in this
    // expression to put the correct LED in the correct state for the next
    // state.  I feel like I need to give the compiler all the help I can.
    digitalWrite(led_pins_[state_ >= 2], !(state_ & 0x1));

    // We nudge the time forward instead of setting it to a specific amount of
    // time in the future, to help us maintain accuracy into the future even if
    // we're slightly behind now.
    last_state_change_time_ += state_durations_[state_];

    state_ = (state_ + 1) & 0x3;
  }
}
