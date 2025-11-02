#ifndef HARDWARE_H
#define HARDWARE_H

#include <FastLED.h>
#include "SystemState.h"

// Initialize hardware and attach the runtime state's LED buffer to FastLED
void hardwareInit(SystemState &state);
void setRelayWind(bool on);
void setRelayElectrolyser(bool on);
bool readButton();

#endif
#ifndef HARDWARE_H
#define HARDWARE_H

#include <FastLED.h>

void hardwareInit(CRGB *leds);
void setRelayWind(bool on);
void setRelayElectrolyser(bool on);
bool readButton();

#endif