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

class StrobeTuner {
  public:
    // Amount of time in microseconds the LEDs are turned on for each cycle.
    static const unsigned int STROBE = 250u;

    StrobeTuner(int led1_pin, int led2_pin) {
      pinMode(led1_pin, OUTPUT);
      pinMode(led2_pin, OUTPUT);
      digitalWrite(led1_pin, LOW);
      digitalWrite(led2_pin, LOW);

      led_pins_[0] = led1_pin;
      led_pins_[1] = led2_pin;
      state_ = 0;
      last_state_change_time_ = 0u;
      note_period_ = 0u;
    }

    // Set the tuning note from its period in microseconds.
    void tune(unsigned int note_period) {
      state_ = (note_period == 0u ? 4 : 0);
      last_state_change_time_ = (unsigned int)micros();
      note_period_ = note_period;

      // Theoretically we should digitalWrite() the pins here to reset them to
      // a known state, but this thing runs so fast you won't notice.
    }

    void update() {
      update((unsigned int)micros());
    }

    void update(unsigned int current_time_) {
      if(state_ >= 4)
        return;

      boolean on = (state_ & 0x1); // Odds are LEDs on.
      unsigned int duration = (on ? STROBE : (note_period_ >> 1) - STROBE);
      // TODO: keep track of cycle_started_time_ or something, to cut down on
      // the error of >> 1 here; set last_state_change_time_ to
      // cycle_started_time_ + note_period_ when the cycle loops.

      if(current_time_ - last_state_change_time_ > duration) {
        int led = (state_ >= 3);
        digitalWrite(led_pins_[led], on);
        state_ = (state_ + 1) & 0x3;
        last_state_change_time_ += duration;
      }
    }

  private:
    int led_pins_[2];
    int state_; // 0: LEDs off, waiting for LED1; 1: LED1 on; 2: LEDs off,
                // waiting for LED2; 3: LED2 on; 4+: not running
    unsigned int last_state_change_time_;
    unsigned int note_period_;
};
