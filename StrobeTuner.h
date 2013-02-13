#ifndef STROBETUNER_H
#define STROBETUNER_H

/*
Example circuit:
___
 A |-(out pin 1) ---|>|-----+
 R |               LED1(R)  |
 D |                        |
 U |-(out pin 2)----|>|-----+
 I |               LED2(G)  |
 N |             ______     |
 O |-(GND) -----|______|----+
___|             R1(160)

*/

class StrobeTuner {
  public:
    // Amount of time in microseconds each LED is turned on each period.
    static const unsigned int STROBE_DURATION = 250u;

    StrobeTuner(int led1_pin, int led2_pin);

    // Set the tuning note from its period in microseconds.  Call once to set
    // the note; update() actually flashes the LEDs.  note_period is the period
    // (1/frequency) of the note to tune to, in microseconds (so actually,
    // 1000000/frequency in Hz).  current_micros is the current value of
    // micros() typecast to unsigned int.
    void tune(unsigned int note_period, unsigned int current_micros);

    // Run the state machine, flashing the LEDs on the pins given in the
    // constructor.  Must be called very frequently to be accurate.
    // current_micros is the current value of micros() typecast to unsigned
    // int.
    void update(unsigned int current_micros);

  private:
    int led_pins_[2];
    int state_;
    unsigned int state_durations_[4];
    unsigned int last_state_change_time_;
};

#endif
