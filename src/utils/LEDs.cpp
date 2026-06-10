#include "../../include/LEDs.h"
// SystemState.h already pulls in Config.h and FastLED
#include "../../include/SystemState.h"

void setPixelSafe(SystemState &state, int idx, const CRGB &col) {
    if ((idx >= 0) && (idx < state.totalLeds)) state.leds[idx] = col;
}

void clearSegment(SystemState &state, int start, int end) {
    if (start < 0) start = 0;
    if (end >= state.totalLeds) end = state.totalLeds - 1;
    if (start >= state.totalLeds) return;
    for (int i = start; i <= end; ++i) state.leds[i] = CRGB::Black;
}

void testAllLeds(SystemState &state, uint16_t delayMs) {
    if (state.totalLeds == 0) return;
    // Clear first
    fill_solid(state.leds, state.totalLeds, CRGB::Black);
    FastLED.show();

    for (int i = 0; i < state.totalLeds; ++i) {
        // light current LED
        state.leds[i] = CRGB::White;
        FastLED.show();
        delay(delayMs);
        // turn it off again before moving on
        state.leds[i] = CRGB::Black;
    }

    // ensure clean state after test
    fill_solid(state.leds, state.totalLeds, CRGB::Black);
    FastLED.show();
}
