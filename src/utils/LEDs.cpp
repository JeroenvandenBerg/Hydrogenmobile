#include "../../include/LEDs.h"
// SystemState.h already pulls in Config.h and FastLED
#include "../../include/SystemState.h"

void setPixelSafe(SystemState &state, int idx, const CRGB &col) {
    if ((unsigned)idx < (unsigned)NUM_LEDS) state.leds[idx] = col;
}

void clearSegment(SystemState &state, int start, int end) {
    if (start < 0) start = 0;
    if (end >= NUM_LEDS) end = NUM_LEDS - 1;
    for (int i = start; i <= end; ++i) state.leds[i] = CRGB::Black;
}

void testAllLeds(SystemState &state, uint16_t delayMs) {
    // Clear first
    fill_solid(state.leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    for (int i = 0; i < NUM_LEDS; ++i) {
        // light current LED
        state.leds[i] = CRGB::White;
        FastLED.show();
        delay(delayMs);
        // turn it off again before moving on
        state.leds[i] = CRGB::Black;
    }

    // ensure clean state after test
    fill_solid(state.leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}
