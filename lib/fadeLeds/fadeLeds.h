#ifndef FADELEDS_H
#define FADELEDS_H

#include <Arduino.h>
#include <FastLED.h>

class fadeLeds {
public:
    fadeLeds(uint32_t fadeDuration, uint32_t unused = 0);

    void update(CRGB* leds, int start, int end, CRGB color, bool& firstRun);

private:
    uint32_t fadeDuration;
    uint32_t previousMillis;
    bool fadeIn;
};

#endif // FADELEDS_H