#include "runningLed.h"

int runningLeds(CRGB* leds, int startLed, int endLed, CRGB COLOR, CRGB DIMCOLOR, uint32_t wait, int currentLed, uint32_t& previousMillis, bool& firstRun) {
    uint32_t currentMillis = millis();  // Get the current time

    // Check if the wait time has passed
    if (currentMillis - previousMillis >= wait) {
        previousMillis = currentMillis;  // Update the last update time

        // Dim the previous LED, but skip on the first run
        if (!firstRun) {
            if (currentLed > startLed) {
                leds[currentLed - 1] = DIMCOLOR;  // Dim the previous LED
            } else if (currentLed == startLed) {
                leds[endLed] = DIMCOLOR;  // Dim the last LED when looping back
            }
        } else {
            // On the first run, ensure all LEDs in the range are off
            fill_solid(leds + startLed, endLed - startLed + 1, CRGB::Black);
        }

        // Set the current LED to the active color
        leds[currentLed] = COLOR;

        // Move to the next LED
        currentLed++;
        if (currentLed > endLed) {
            currentLed = startLed;  // Loop back to the start LED
        }

        // Mark the first run as complete
        firstRun = false;
    }

    return currentLed;  // Return the updated current LED
    Serial.print("Current LED: ");
    Serial.println(currentLed);
    Serial.print("Dimming LED: ");
    if (currentLed > startLed) {
        Serial.println(currentLed - 1);
    } else if (currentLed == startLed) {
        Serial.println(endLed);
}
}

int reverseRunningLeds(CRGB* leds, int startLed, int endLed, CRGB COLOR, CRGB DIMCOLOR, uint32_t wait, int currentLed, uint32_t& previousMillis, bool& firstRun) {
    uint32_t currentMillis = millis();  // Get the current time

    // Check if the wait time has passed
    if (currentMillis - previousMillis >= wait) {
        previousMillis = currentMillis;  // Update the last update time

        // Dim the previous LED, but skip on the first run
        if (!firstRun) {
            if (currentLed < endLed) {
                leds[currentLed + 1] = DIMCOLOR;  // Dim the next LED
            } else if (currentLed == endLed) {
                leds[startLed] = DIMCOLOR;  // Dim the first LED when looping back
            }
        }

        // Set the current LED to the active color
        leds[currentLed] = COLOR;

        // Move to the previous LED
        currentLed--;
        if (currentLed < startLed) {
            currentLed = endLed;  // Loop back to the end LED
        }

        // Mark the first run as complete
        firstRun = false;
    }

    return currentLed;  // Return the updated current LED
}