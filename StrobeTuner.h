#include "Arduino.h"

/*
Example circuit:

- (out pin 1) ---|>|-----+
                LED1(R)  |
                         |
- (out pin 2) ---|>|-----+
                LED2(G)  |
              ______     |
- (GND) -----|______|----+
              R1(160)
*/

#define NUM_STATES 4

class StrobeTuner {
  public:
    // Amount of time in microseconds the LEDs are turned on for each cycle.
    static const unsigned int STROBE_DURATION = 250u;

    StrobeTuner(int led1_pin, int led2_pin) {
      pinMode(led1_pin, OUTPUT);
      pinMode(led2_pin, OUTPUT);
      digitalWrite(led1_pin, LOW);
      digitalWrite(led2_pin, LOW);

      led_pins_[0] = led1_pin;
      led_pins_[1] = led2_pin;
    }

    // Set the tuning note from its period in microseconds.
    void tune(unsigned int note_period) {
      unsigned int off = (note_period >> 1) - STROBE_DURATION;

      state_ = (note_period == 0u ? NUM_STATES : 0);
      state_durations_[0] = off;
      state_durations_[1] = STROBE_DURATION;
      state_durations_[2] = note_period - off - (STROBE_DURATION << 1);
      state_durations_[3] = STROBE_DURATION;
      last_state_change_time_ = (unsigned int)micros();

      digitalWrite(led_pins_[0], LOW);
      digitalWrite(led_pins_[1], LOW);
    }

    inline void update() {
      update((unsigned int)micros());
    }

    void update(unsigned int current_time_) {
      if(state_ >= NUM_STATES)
        return;

      if(current_time_ - last_state_change_time_ >= state_durations_[state_]) {
        digitalWrite(led_pins_[state_ >= 2], !(state_ & 0x1));
        last_state_change_time_ += state_durations_[state_];
        state_ = (state_ + 1) & 0x3;
      }
    }

  private:
    int led_pins_[2];
    int state_; // 0: LEDs off, waiting for LED1; 1: LED1 on; 2: LEDs off,
                // waiting for LED2; 3: LED2 on; 4+: not running
    unsigned int state_durations_[4];
    unsigned int last_state_change_time_;
};
