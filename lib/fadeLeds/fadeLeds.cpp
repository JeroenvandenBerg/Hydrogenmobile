#include "fadeLeds.h"

fadeLeds::fadeLeds(uint32_t fadeDuration, uint32_t)
    : fadeDuration(fadeDuration), previousMillis(0), fadeIn(true) {}

void fadeLeds::update(CRGB* leds, int start, int end, CRGB color, bool& firstRun) {
    uint32_t currentMillis = millis();
    uint32_t elapsed = currentMillis - previousMillis;

    // Handle the first run to initialize LEDs at 5% brightness
    if (firstRun) {
        float initialBrightness = 0.05;  // 5% brightness
        for (int i = start; i <= end; i++) {
            leds[i] = CRGB(
                color.r * initialBrightness,
                color.g * initialBrightness,
                color.b * initialBrightness
            );
        }
        firstRun = false;  // Mark the first run as complete
        previousMillis = currentMillis;  // Start the timer
        return;
    }

    // Check if it's time to switch between fade-in and fade-out
    if (elapsed >= fadeDuration) {
        fadeIn = !fadeIn;  // Toggle between fading in and out
        previousMillis = currentMillis;  // Reset the timer
        elapsed = 0;  // Reset elapsed time for smooth transition
    }

    // Calculate the fade progress
    float progress = (float)elapsed / fadeDuration;
    if (fadeIn) {
        progress = 0.05 + (progress * 0.95);  // Scale progress from 5% to 100%
    } else {
        progress = 1.0 - (progress * 0.95);  // Scale progress from 100% to 5%
    }

    // Apply the fade effect to the LEDs
    for (int i = start; i <= end; i++) {
        leds[i] = CRGB(
            color.r * progress,
            color.g * progress,
            color.b * progress
        );
    }
}