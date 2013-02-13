/*
  StrobeTuner - Arduino library for simple strobe tuners
                <https://github.com/chazomaticus/strobetuner>
  Copyright 2013 Charles Lindsay <chaz@chazomatic.us>

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

#include <StrobeTuner.h>

// My arduino's clock speed seems to be sliiiiightly slower than 16MHz, so any
// value I pass as a duration needs to be adjusted.  Unfortunately, to figure
// out the correct value here for your arduino means either some sophisticated
// timing equipment or having another strobe tuner on hand to compare it with.
// Good luck.  You may find it works adequately without a multiplier here (i.e.
// with this set to 1.0f).
static const float CLOCK_SPEED = 0.999f;

// See StrobeTuner.h for an example circuit.  In this case, "LED1 pin" is 7 and
// "LED2 pin" is 8.
StrobeTuner tuner(7, 8);

void setup() {
  // Set the tuner to let us tune high E.
  tuner.tune(StrobeTuner::PERIOD_E4 * CLOCK_SPEED, (unsigned int)micros());
}

void loop() {
  tuner.update((unsigned int)micros());
}
