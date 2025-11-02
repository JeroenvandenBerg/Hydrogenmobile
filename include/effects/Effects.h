#ifndef EFFECTS_H
#define EFFECTS_H

#include "../SystemState.h"

// Effects now accept a reference to the centralized SystemState and Timers
void updateWindEffect(SystemState &state, Timers &timers);
void updateElectricityProductionEffect(SystemState &state, Timers &timers);
void updateElectrolyserEffect(SystemState &state, Timers &timers);
void updateHydrogenProductionEffect(SystemState &state, Timers &timers);
void updateHydrogenTransportEffect(SystemState &state, Timers &timers);
void updateHydrogenStorageEffect(SystemState &state, Timers &timers);
void updateH2ConsumptionEffect(SystemState &state, Timers &timers);
void updateFabricationEffect(SystemState &state, Timers &timers);
void updateStorageTransportEffect(SystemState &state, Timers &timers);
void updateElectricityEffect(SystemState &state, Timers &timers);
void updateInformationLEDs(SystemState &state, Timers &timers);

#endif
