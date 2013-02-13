StrobeTuner
===========

StrobeTuner is an Arduino library for creating simple "strobe" guitar tuners.

<https://github.com/chazomaticus/strobetuner>

About
-----

A while back I bought a little "strobe" tuner (see [MusiciansFriend][1] or
[Amazon][2]) for my guitar.  I use "strobe" in quotes because it turns out it's
just a couple of LEDs on a variable timer.  I've been learning my way around an
Arduino, and thought I could make my own "strobe" tuner for pretty cheap, with
the added benefit of being able to tune to any note I want.

Hence, StrobeTuner, an Arduino library for creating your own "strobe" tuner out
of two LEDs and a resistor.  See
[StrobeTuner.h][https://raw.github.com/chazomaticus/strobetuner/master/StrobeTuner.h]
for the example circuit, or see the image below.  You might want to mount the
components somewhere you can easily shine the LEDs on your guitar strings.

![Example Circuit](https://raw.github.com/chazomaticus/strobetuner/master/strobetuner-circuit.jpg)

Accuracy
--------

When I compared my implementation to the real "strobe" tuner, I noticed my
version was slightly off.  I don't see anything in the code that would cause
the discrepancy, so I'm chalking it up to error in the Arduino's oscillator
frequency.  We're talking a around tenth of a percent slower than it should be.
In the example, you can see I just multiply the note's period by 99.9%, which
fixes the error on my Arduino.

It's also possible I just don't understand precise timing on the Arduino yet,
and my code is rife with bugs.  Your mileage may vary.

Installation
------------

To install, put this code in a subdirectory of your Arduino sketchbook's
libraries directory.  See <http://arduino.cc/en/Guide/Libraries> for details.


Enjoy!


[1]: http://www.musiciansfriend.com/accessories/planet-waves-sos-strobe-pick-tuner
[2]: http://www.amazon.com/dp/B000FJEL9C/
