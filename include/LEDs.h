#ifndef LEDS_H
#define LEDS_H

#include <FastLED.h>
#include "SystemState.h"

// LED helpers that operate on the system state's LED buffer
void setPixelSafe(SystemState &state, int idx, const CRGB &col);
void clearSegment(SystemState &state, int start, int end);
// Run a quick LED test by lighting each LED in sequence (from 0 to NUM_LEDS-1)
// delayMs is the milliseconds to wait between steps (default 20ms)
void testAllLeds(SystemState &state, uint16_t delayMs = 500);

#endif
