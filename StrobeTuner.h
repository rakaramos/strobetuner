/*
  StrobeTuner - Arduino library for simple strobe tuners
  Copyright 2013 Charles Lindsay

  StrobeTuner is free software: you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation, either version 3 of the License, or (at your option) any
  later version.

  StrobeTuner is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public License
  along with StrobeTuner.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef STROBETUNER_H
#define STROBETUNER_H

/*
Example circuit:
___
 A |-(LED1 pin) ---|>|-----+
 R |              LED1(R)  |
 D |                       |
 U |-(LED2 pin)----|>|-----+
 I |              LED2(G)  |
 N |             ______    |
 O |-(GND) -----|______|---+
___|             R1(160)

*/

class StrobeTuner {
  public:
    // Standard 7-string tuning, period (1/frequency of note) in microseconds.
    static const unsigned int PERIOD_B1 = 16198u; // 1/61.735 Hz, in us.
    static const unsigned int PERIOD_E2 = 12135u; // 1/82.407 Hz, in us.
    static const unsigned int PERIOD_A2 = 9091u; // 1/110.00 Hz, in us.
    static const unsigned int PERIOD_D3 = 6811u; // 1/146.83 Hz, in us.
    static const unsigned int PERIOD_G3 = 5102u; // 1/196.00 Hz, in us.
    static const unsigned int PERIOD_B3 = 4050u; // 1/246.94 Hz, in us.
    static const unsigned int PERIOD_E4 = 3034u; // 1/329.63 Hz, in us.
    // To get the values for other notes, try searching wolframalpha.com for
    // "frequency of note c3" or some other note, and doing the arithmetic.

    // Amount of time in microseconds each LED is turned on each period.
    static const unsigned int STROBE_DURATION = 250u;

    StrobeTuner(int led1_pin, int led2_pin);

    // Set the tuning note from its period in microseconds.  Call once to set
    // the note; update() actually flashes the LEDs.  note_period is the period
    // (1/frequency) of the note to tune to, in microseconds (so actually,
    // 1000000/frequency in Hz).  Pass 0 to turn off the tuner.  current_micros
    // is the current value of micros() typecast to unsigned int.
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
