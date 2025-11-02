#ifndef HARDWARE_H
#define HARDWARE_H

#include <FastLED.h>

void hardwareInit(CRGB *leds);
void setRelayWind(bool on);
void setRelayElectrolyser(bool on);
bool readButton();

#endif