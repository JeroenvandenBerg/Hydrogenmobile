#ifndef RUNNINGLED_H
#define RUNNINGLED_H

#include <FastLED.h>

// Function declarations
int runningLeds(CRGB* leds, int startLed, int endLed, CRGB COLOR, CRGB DIMCOLOR, uint32_t wait, int currentLed, uint32_t& previousMillis, bool& firstRun);
int reverseRunningLeds(CRGB* leds, int startLed, int endLed, CRGB COLOR, CRGB DIMCOLOR, uint32_t wait, int currentLed, uint32_t& previousMillis, bool& firstRun);

#endif // RUNNINGLED_H