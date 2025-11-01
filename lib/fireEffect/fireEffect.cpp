#include "fireEffect.h"

// Fire effect function
void fireEffect(CRGB* leds, int startLed, int endLed) {
    static uint8_t heat[256];  // Array to store heat values for each LED
    static uint32_t previousMillis = 0;  // Stores the last update time
    const uint8_t cooling = 55;         // Default cooling value
    const uint8_t sparking = 120;       // Default sparking value
    const uint32_t wait = 50;           // Default wait time in milliseconds

    uint32_t currentMillis = millis();

    // Check if the wait time has passed
    if (currentMillis - previousMillis >= wait) {
        previousMillis = currentMillis;  // Update the last update time

        // Step 1: Cool down every cell a little
        for (int i = startLed; i <= endLed; i++) {
            heat[i] = qsub8(heat[i], random8(0, ((cooling * 10) / (endLed - startLed + 1)) + 2));
        }

        // Step 2: Heat from each cell drifts 'up' and diffuses a little
        for (int i = endLed; i >= startLed + 2; i--) {
            heat[i] = (heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3;
        }

        // Step 3: Randomly ignite new sparks near the bottom
        if (random8() < sparking) {
            int y = random8(startLed, startLed + 7);
            heat[y] = qadd8(heat[y], random8(160, 255));
        }

        // Step 4: Map from heat cells to LED colors with more red
        for (int i = startLed; i <= endLed; i++) {
            CRGB color = HeatColor(heat[i]);

            // Adjust the color to emphasize red tones
            color.r = qadd8(color.r, 50);  // Boost red intensity
            color.g = scale8(color.g, 150);  // Reduce green slightly to make red more dominant
            color.b = 0;  // Remove blue entirely for a more natural fire look

            // Reduce brightness at the highest heat levels to avoid white
            if (heat[i] > 200) {
                color.r = scale8(color.r, 240);  // Scale down red slightly at high heat
                color.g = scale8(color.g, 120);  // Scale down green more aggressively
            }

            leds[i] = color;
        }
    }
}