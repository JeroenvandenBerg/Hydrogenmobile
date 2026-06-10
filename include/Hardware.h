#ifndef HARDWARE_H
#define HARDWARE_H

#include <FastLED.h>
#include "SystemState.h"

// Initialize hardware and attach the runtime state's LED buffer to FastLED
void hardwareInit(SystemState &state);
// Apply pin configuration after startup or after changing pin settings
void applyPinConfiguration(SystemState &state);
// Reconfigure the FastLED controller to match the current state.totalLeds value
void applyLedCount(SystemState &state);
void setRelayWind(bool on);
void setRelayElectrolyser(bool on);
bool readButton();

#endif