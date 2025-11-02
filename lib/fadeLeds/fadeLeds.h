#ifndef FADELEDS_H
#define FADELEDS_H

#include <Arduino.h>
#include <FastLED.h>

class fadeLeds {
public:
    fadeLeds(uint32_t fadeDuration, uint32_t unused = 0);

    void update(CRGB* leds, int start, int end, CRGB color, bool& firstRun);
    // Overload that allows overriding fade duration per-call (in milliseconds)
    void update(CRGB* leds, int start, int end, CRGB color, bool& firstRun, uint32_t durationOverride);

    // Optional setter if you want to change the default duration globally
    inline void setDuration(uint32_t d) { fadeDuration = d; }

private:
    uint32_t fadeDuration;
    uint32_t previousMillis;
    bool fadeIn;
};

#endif // FADELEDS_H