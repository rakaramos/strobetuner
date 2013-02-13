#include <StrobeTuner.h>

// My arduino's clock speed seems to be sliiiiightly slower than 16MHz, so any
// value I pass as a duration needs to be adjusted.  Unfortunately, to figure
// out the correct value here for your arduino means either some sophisticated
// timing equipment or having another strobe tuner on hand to compare it with.
// Good luck.  You may find it works adequately without a multiplier here.
#define CLOCK_SPEED 0.999f

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
