#include "../../include/effects/Effects.h"
#include "../../include/Config.h"
#include "../../include/LEDs.h"
#include "../../lib/fadeLeds/fadeLeds.h"
#include "../../lib/runningLed/runningLed.h"
#include "../../lib/fireEffect/fireEffect.h"
#include "../../include/SystemState.h"
#include "../../include/effects/EffectUtils.h"
#include <Arduino.h>

// fadeEffect is owned by the runtime SystemState (state.fadeEffect)

// ---- Wind effect
void updateWindEffect(SystemState &state, Timers &timers) {
    if (state.windOn) {
        // Wind segment
        if (state.windEnabled) {
            if (state.windEffectType == 1) {
                fireEffect(state.leds, state.windSegmentStart, state.windSegmentEnd);
                EffectUtils::advanceIndexDir(state.windDelay,
                                             state.windSegmentStart,
                                             state.windSegmentEnd,
                                             state.windDirForward,
                                             state.windSegment,
                                             timers.previousMillisWind,
                                             state.firstRunWind);
            } else {
                state.windSegment = EffectUtils::runSegmentDir(
                    state,
                    state.windSegmentStart,
                    state.windSegmentEnd,
                    WIND_COLOR_ACTIVE,
                    CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
                    state.windDelay,
                    state.windSegment,
                    timers.previousMillisWind,
                    state.firstRunWind,
                    state.windDirForward
                );
            }
        } else {
            EffectUtils::clearRange(state, state.windSegmentStart, state.windSegmentEnd);
            state.firstRunWind = true;
            state.windSegment = EffectUtils::initialIndex(state.windDirForward, state.windSegmentStart, state.windSegmentEnd);
        }

        // Solar segment
        if (state.solarEnabled) {
            if (state.solarEffectType == 1) {
                fireEffect(state.leds, state.solarSegmentStart, state.solarSegmentEnd);
                EffectUtils::advanceIndexDir(state.solarDelay,
                                             state.solarSegmentStart,
                                             state.solarSegmentEnd,
                                             state.solarDirForward,
                                             state.solarSegment,
                                             timers.previousMillisSolar,
                                             state.firstRunSolar);
            } else {
                state.solarSegment = EffectUtils::runSegmentDir(
                    state,
                    state.solarSegmentStart,
                    state.solarSegmentEnd,
                    WIND_COLOR_ACTIVE,
                    CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
                    state.solarDelay,
                    state.solarSegment,
                    timers.previousMillisSolar,
                    state.firstRunSolar,
                    state.solarDirForward
                );
            }
        } else {
            EffectUtils::clearRange(state, state.solarSegmentStart, state.solarSegmentEnd);
            state.firstRunSolar = true;
            state.solarSegment = EffectUtils::initialIndex(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd);
        }

        // Trigger electricity production if enabled and if at least one enabled source has reached terminal
        bool windReachedTerminal = state.windEnabled && (state.windSegment == EffectUtils::terminalBound(state.windDirForward, state.windSegmentStart, state.windSegmentEnd));
        bool solarReachedTerminal = state.solarEnabled && (state.solarSegment == EffectUtils::terminalBound(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd));
        
        if (state.electricityProductionEnabled && (windReachedTerminal || solarReachedTerminal)) {
            state.electricityProductionOn = true;
        }
    } else {
        EffectUtils::clearRange(state, state.windSegmentStart, state.windSegmentEnd);
        state.firstRunWind = true;
        state.windSegment = EffectUtils::initialIndex(state.windDirForward, state.windSegmentStart, state.windSegmentEnd);

        EffectUtils::clearRange(state, state.solarSegmentStart, state.solarSegmentEnd);
        state.firstRunSolar = true;
        state.solarSegment = EffectUtils::initialIndex(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd);
        state.electricityProductionOn = false;
    }
}

// ---- Electricity production effect
void updateElectricityProductionEffect(SystemState &state, Timers &timers) {
    if (state.electricityProductionOn && state.electricityProductionEnabled) {
        if (state.electricityProductionEffectType == 1) {
            fireEffect(state.leds, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd);
            EffectUtils::advanceIndexDir(state.electricityProductionDelay,
                                         state.electricityProductionSegmentStart,
                                         state.electricityProductionSegmentEnd,
                                         state.electricityProductionDirForward,
                                         state.electricityProductionSegment,
                                         timers.previousMillisElectricityProduction,
                                         state.firstRunElectricityProduction);
        } else {
            state.electricityProductionSegment = EffectUtils::runSegmentDir(
                state,
                state.electricityProductionSegmentStart,
                state.electricityProductionSegmentEnd,
                WIND_COLOR_ACTIVE,
                CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
                state.electricityProductionDelay,
                state.electricityProductionSegment,
                timers.previousMillisElectricityProduction,
                state.firstRunElectricityProduction,
                state.electricityProductionDirForward
            );
        }

        if (state.electricityProductionSegment == EffectUtils::terminalBound(state.electricityProductionDirForward, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd)) {
            if (!state.electrolyserOn && state.electrolyserEnabled) {
                state.electrolyserOn = true;
                timers.previousMillisElectrolyser = millis();
            }
        }
    } else {
        EffectUtils::clearRange(state, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd);
        state.firstRunElectricityProduction = true;
        state.electricityProductionSegment = EffectUtils::initialIndex(state.electricityProductionDirForward, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd);
        state.electrolyserOn = false;
    }
}

// ---- Electrolyser
void updateElectrolyserEffect(SystemState &state, Timers &timers) {
    if (state.electrolyserOn && state.electrolyserEnabled) {
        if (millis() - timers.previousMillisElectrolyser >= HYDROGEN_PRODUCTION_DELAY_MS) {
            if (state.hydrogenProductionEnabled) {
                state.hydrogenProductionOn = true;
            }
        }
    } else {
        state.hydrogenProductionOn = false;
    }
}

// ---- Hydrogen production/transport/storage/consumption (moved here)
void updateHydrogenProductionEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenProductionOn && state.hydrogenProductionEnabled) {
        if (state.fadeEffect) {
            state.fadeEffect->update(state.leds, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd, HYDROGEN_PRODUCTION_COLOR_ACTIVE, state.firstRunHydrogenProduction);
        }
        if (state.hydrogenTransportEnabled) {
            state.hydrogenTransportOn = true;
        }
    } else {
        EffectUtils::clearRange(state, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd);
        state.firstRunHydrogenProduction = true;
        state.hydrogenTransportOn = false;
    }
}

void updateHydrogenTransportEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenTransportOn && state.hydrogenTransportEnabled) {
        if (state.hydrogenTransportEffectType == 1) {
            fireEffect(state.leds, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
            EffectUtils::advanceIndexDir(state.hydrogenTransportDelay,
                                         state.hydrogenTransportSegmentStart,
                                         state.hydrogenTransportSegmentEnd,
                                         state.hydrogenTransportDirForward,
                                         state.hydrogenTransportSegment,
                                         timers.previousMillisHydrogenTransport,
                                         state.firstRunHydrogenTransport);
        } else {
            state.hydrogenTransportSegment = EffectUtils::runSegmentDir(
                state,
                state.hydrogenTransportSegmentStart,
                state.hydrogenTransportSegmentEnd,
                HYDROGEN_PRODUCTION_COLOR_ACTIVE,
                CRGB(HYDROGEN_PRODUCTION_COLOR_ACTIVE.r / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.g / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.b / 10),
                state.hydrogenTransportDelay,
                state.hydrogenTransportSegment,
                timers.previousMillisHydrogenTransport,
                state.firstRunHydrogenTransport,
                state.hydrogenTransportDirForward
            );
        }

        if (state.hydrogenTransportSegment == HYDROGEN_TRANSPORT_LED_MID) {
            if (state.h2ConsumptionEnabled) {
                state.h2ConsumptionOn = true;
            }
        }
        if (state.hydrogenTransportSegment == EffectUtils::terminalBound(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd)) {
            if (state.hydrogenStorageEnabled) {
                state.hydrogenStorageOn = true;
            }
            state.emptyPipe = true;
        }
    } else if (state.hydrogenStorageFull && state.hydrogenTransportEnabled) {
        if (state.hydrogenTransportSegment == EffectUtils::initialIndex(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd)) {
            state.pipeEmpty = true;
        }

        if (state.emptyPipe) {
            fill_solid(state.leds + state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd - state.hydrogenTransportSegmentStart + 1,
                       CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 20, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 20, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 20));
            state.hydrogenTransportSegment = EffectUtils::initialIndex(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
            state.emptyPipe = false;
        }

        if (!state.pipeEmpty) {
            if (state.hydrogenTransportEffectType == 1) {
                fireEffect(state.leds, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
                EffectUtils::advanceIndexDir(state.hydrogenTransportDelay,
                                             state.hydrogenTransportSegmentStart,
                                             state.hydrogenTransportSegmentEnd,
                                             state.hydrogenTransportDirForward,
                                             state.hydrogenTransportSegment,
                                             timers.previousMillisHydrogenTransport,
                                             state.firstRunHydrogenTransport);
            } else {
                state.hydrogenTransportSegment = EffectUtils::runSegmentDir(
                    state,
                    state.hydrogenTransportSegmentStart,
                    state.hydrogenTransportSegmentEnd,
                    HYDROGEN_PRODUCTION_COLOR_ACTIVE,
                    CRGB::Black,
                    state.hydrogenTransportDelay,
                    state.hydrogenTransportSegment,
                    timers.previousMillisHydrogenTransport,
                    state.firstRunHydrogenTransport,
                    state.hydrogenTransportDirForward
                );
            }
        } else {
            EffectUtils::clearRange(state, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
        }

        state.hydrogenStorageOn = false;
    } else {
        // reset or disabled
        EffectUtils::clearRange(state, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
        state.firstRunHydrogenTransport = true;
        state.hydrogenTransportSegment = EffectUtils::initialIndex(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
        state.hydrogenStorageOn = false;
        state.emptyPipe = false;
        state.pipeEmpty = false;
    }
}

void updateHydrogenStorageEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenStorageOn && state.hydrogenStorageEnabled) {
        if (state.hydrogenStorage1EffectType == 1) {
            fireEffect(state.leds, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
            EffectUtils::advanceIndexDir(state.hydrogenStorage1Delay,
                                         state.hydrogenStorage1SegmentStart,
                                         state.hydrogenStorage1SegmentEnd,
                                         state.hydrogenStorage1DirForward,
                                         state.hydrogenStorageSegment1,
                                         timers.previousMillisHydrogenStorage,
                                         state.firstRunHydrogenStorage);
        } else {
            state.hydrogenStorageSegment1 = EffectUtils::runSegmentDir(
                state,
                state.hydrogenStorage1SegmentStart,
                state.hydrogenStorage1SegmentEnd,
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                state.hydrogenStorage1Delay,
                state.hydrogenStorageSegment1,
                timers.previousMillisHydrogenStorage,
                state.firstRunHydrogenStorage,
                state.hydrogenStorage1DirForward
            );
        }
        if (state.hydrogenStorage2EffectType == 1) {
            fireEffect(state.leds, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
            EffectUtils::advanceIndexDir(state.hydrogenStorage2Delay,
                                         state.hydrogenStorage2SegmentStart,
                                         state.hydrogenStorage2SegmentEnd,
                                         state.hydrogenStorage2DirForward,
                                         state.hydrogenStorageSegment2,
                                         timers.previousMillisHydrogenStorage2,
                                         state.firstRunHydrogenStorage2);
        } else {
            state.hydrogenStorageSegment2 = EffectUtils::runSegmentDir(
                state,
                state.hydrogenStorage2SegmentStart,
                state.hydrogenStorage2SegmentEnd,
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                state.hydrogenStorage2Delay,
                state.hydrogenStorageSegment2,
                timers.previousMillisHydrogenStorage2,
                state.firstRunHydrogenStorage2,
                state.hydrogenStorage2DirForward
            );
        }

        if (state.hydrogenStorageSegment1 == EffectUtils::terminalBound(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd)) {
            state.hydrogenStorageFull = true;
        }
    } else if (state.hydrogenStorageFull) {
        if (!state.storageTimerStarted) {
            state.h2ConsumptionOn = false;
            fill_solid(state.leds + state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd - state.hydrogenStorage1SegmentStart + 1, CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10));
            fill_solid(state.leds + state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd - state.hydrogenStorage2SegmentStart + 1, CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10));
            state.h2ConsumptionSegment = EffectUtils::initialIndex(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
            state.hydrogenStorageSegment1 = EffectUtils::terminalBound(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
            state.hydrogenStorageSegment2 = EffectUtils::terminalBound(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
            timers.hydrogenStorageFullTimer = millis();
            state.storageTimerStarted = true;
        }
        if (millis() - timers.hydrogenStorageFullTimer >= HYDROGEN_STORAGE_DELAY_MS) {
            if (state.hydrogenStorage1EffectType == 1) {
                fireEffect(state.leds, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
                EffectUtils::advanceIndexDir(state.hydrogenStorage1Delay,
                                             state.hydrogenStorage1SegmentStart,
                                             state.hydrogenStorage1SegmentEnd,
                                             !state.hydrogenStorage1DirForward,
                                             state.hydrogenStorageSegment1,
                                             timers.previousMillisHydrogenStorage,
                                             state.firstRunHydrogenStorage);
            } else {
                state.hydrogenStorageSegment1 = EffectUtils::runSegmentDir(
                    state,
                    state.hydrogenStorage1SegmentStart,
                    state.hydrogenStorage1SegmentEnd,
                    HYDROGEN_STORAGE_COLOR_ACTIVE,
                    CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                    state.hydrogenStorage1Delay,
                    state.hydrogenStorageSegment1,
                    timers.previousMillisHydrogenStorage,
                    state.firstRunHydrogenStorage,
                    !state.hydrogenStorage1DirForward
                );
            }
            if (state.hydrogenStorage2EffectType == 1) {
                fireEffect(state.leds, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
                EffectUtils::advanceIndexDir(state.hydrogenStorage2Delay,
                                             state.hydrogenStorage2SegmentStart,
                                             state.hydrogenStorage2SegmentEnd,
                                             !state.hydrogenStorage2DirForward,
                                             state.hydrogenStorageSegment2,
                                             timers.previousMillisHydrogenStorage2,
                                             state.firstRunHydrogenStorage2);
            } else {
                state.hydrogenStorageSegment2 = EffectUtils::runSegmentDir(
                    state,
                    state.hydrogenStorage2SegmentStart,
                    state.hydrogenStorage2SegmentEnd,
                    HYDROGEN_STORAGE_COLOR_ACTIVE,
                    CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                    state.hydrogenStorage2Delay,
                    state.hydrogenStorageSegment2,
                    timers.previousMillisHydrogenStorage2,
                    state.firstRunHydrogenStorage2,
                    !state.hydrogenStorage2DirForward
                );
            }
        }
        if (state.hydrogenStorageSegment1 == EffectUtils::initialIndex(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd) ||
            state.hydrogenStorageSegment2 == EffectUtils::initialIndex(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd)) {
            if (state.storageTransportEnabled) {
                state.storageTransportOn = true;
            }
        }
    } else {
        EffectUtils::clearRange(state, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
        EffectUtils::clearRange(state, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
        state.firstRunHydrogenStorage = true;
        state.firstRunHydrogenStorage2 = true;
        state.hydrogenStorageSegment1 = EffectUtils::initialIndex(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
        state.hydrogenStorageSegment2 = EffectUtils::initialIndex(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
        state.storageTransportOn = false;
        state.storageTimerStarted = false;
    }
}

void updateH2ConsumptionEffect(SystemState &state, Timers &timers) {
    if (state.h2ConsumptionOn && state.h2ConsumptionEnabled) {
        if (state.h2ConsumptionEffectType == 1) {
            fireEffect(state.leds, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
            EffectUtils::advanceIndexDir(state.h2ConsumptionDelay,
                                         state.hydrogenConsumptionSegmentStart,
                                         state.hydrogenConsumptionSegmentEnd,
                                         state.h2ConsumptionDirForward,
                                         state.h2ConsumptionSegment,
                                         timers.previousMillisH2Consumption,
                                         state.firstRunH2Consumption);
        } else {
            state.h2ConsumptionSegment = EffectUtils::runSegmentDir(
                state,
                state.hydrogenConsumptionSegmentStart,
                state.hydrogenConsumptionSegmentEnd,
                HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
                CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
                state.h2ConsumptionDelay,
                state.h2ConsumptionSegment,
                timers.previousMillisH2Consumption,
                state.firstRunH2Consumption,
                state.h2ConsumptionDirForward
            );
        }

        if (state.h2ConsumptionSegment == EffectUtils::terminalBound(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd)) {
            if (state.fabricationEnabled) {
                state.fabricationOn = true;
            }
        }
    } else if (state.storageTransportOn && state.storageTransportEnabled) {
        if (state.storageTransportSegment == state.storageTransportSegmentEnd) {
            if (state.fabricationEnabled) {
                state.fabricationOn = true;
            }
        }
    } else {
        EffectUtils::clearRange(state, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
        state.firstRunH2Consumption = true;
        state.h2ConsumptionSegment = EffectUtils::initialIndex(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
        state.fabricationOn = false;
    }
}

// ---- Fabrication effect
void updateFabricationEffect(SystemState &state, Timers &timers) {
    if (state.fabricationOn && state.fabricationEnabled) {
        fireEffect(state.leds, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    } else {
        EffectUtils::clearRange(state, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    }
}

// ---- Storage transport / powerstation
void updateStorageTransportEffect(SystemState &state, Timers &timers) {
    if (state.storageTransportOn && state.storageTransportEnabled) {
        if (state.storageTransportEffectType == 1) {
            fireEffect(state.leds, state.storageTransportSegmentStart, state.storageTransportSegmentEnd);
            EffectUtils::advanceIndexDir(state.storageTransportDelay,
                                         state.storageTransportSegmentStart,
                                         state.storageTransportSegmentEnd,
                                         state.storageTransportDirForward,
                                         state.storageTransportSegment,
                                         timers.previousMillisStorageTransport,
                                         state.firstRunStorageTransport);
        } else {
            state.storageTransportSegment = EffectUtils::runSegmentDir(
                state,
                state.storageTransportSegmentStart,
                state.storageTransportSegmentEnd,
                HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
                CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
                state.storageTransportDelay,
                state.storageTransportSegment,
                timers.previousMillisStorageTransport,
                state.firstRunStorageTransport,
                state.storageTransportDirForward
            );
        }
        if (state.storageTransportSegment == EffectUtils::terminalBound(state.storageTransportDirForward, state.storageTransportSegmentStart, state.storageTransportSegmentEnd)) {
            if (state.storagePowerstationEnabled) {
                state.storagePowerstationOn = true;
            }
        }
        if (state.storagePowerstationOn && state.storagePowerstationEnabled) {
            if (state.storagePowerstationEffectType == 1) {
                fireEffect(state.leds, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);
                EffectUtils::advanceIndexDir(state.storagePowerstationDelay,
                                             state.storagePowerstationSegmentStart,
                                             state.storagePowerstationSegmentEnd,
                                             state.storagePowerstationDirForward,
                                             state.storagePowerstationSegment,
                                             timers.previousMillisStoragePowerstation,
                                             state.firstRunStoragePowerstation);
            } else {
                state.storagePowerstationSegment = EffectUtils::runSegmentDir(
                    state,
                    state.storagePowerstationSegmentStart,
                    state.storagePowerstationSegmentEnd,
                    HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
                    CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
                    state.storagePowerstationDelay,
                    state.storagePowerstationSegment,
                    timers.previousMillisStoragePowerstation,
                    state.firstRunStoragePowerstation,
                    state.storagePowerstationDirForward
                );
            }
        }
        if (state.storagePowerstationSegment == EffectUtils::terminalBound(state.storagePowerstationDirForward, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd)) {
            if (state.electricityTransportEnabled) {
                state.electricityTransportOn = true;
                Serial.println("Electricity transport enabled");
            }
        }
    } else {
        EffectUtils::clearRange(state, state.storageTransportSegmentStart, state.storageTransportSegmentEnd);
        state.firstRunStorageTransport = true;
        state.storageTransportSegment = EffectUtils::initialIndex(state.storageTransportDirForward, state.storageTransportSegmentStart, state.storageTransportSegmentEnd);
        EffectUtils::clearRange(state, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);
        state.firstRunStoragePowerstation = true;
        state.storagePowerstationSegment = EffectUtils::initialIndex(state.storagePowerstationDirForward, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);
        state.storagePowerstationOn = false;
    }
}

// ---- Electricity transport
void updateElectricityEffect(SystemState &state, Timers &timers) {
    if (state.electricityTransportOn && state.electricityTransportEnabled) {
        if (state.electricityTransportEffectType == 1) {
            fireEffect(state.leds, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
            EffectUtils::advanceIndexDir(state.electricityTransportDelay,
                                         state.electricityTransportSegmentStart,
                                         state.electricityTransportSegmentEnd,
                                         state.electricityTransportDirForward,
                                         state.electricityTransportSegment,
                                         timers.previousMillisElectricityTransport,
                                         state.firstRunElectricityTransport);
        } else {
            state.electricityTransportSegment = EffectUtils::runSegmentDir(
                state,
                state.electricityTransportSegmentStart,
                state.electricityTransportSegmentEnd,
                ELECTRICITY_TRANSPORT_COLOR_ACTIVE,
                CRGB(ELECTRICITY_TRANSPORT_COLOR_ACTIVE.r / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.g / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.b / 10),
                state.electricityTransportDelay,
                state.electricityTransportSegment,
                timers.previousMillisElectricityTransport,
                state.firstRunElectricityTransport,
                state.electricityTransportDirForward
            );
        }

        if (state.electricityTransportSegment == EffectUtils::terminalBound(state.electricityTransportDirForward, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd)) {
            digitalWrite(STREET_LED_PIN, HIGH);
            state.streetLightOn = true;
        }
    } else {
        EffectUtils::clearRange(state, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
        state.firstRunElectricityTransport = true;
        state.electricityTransportSegment = EffectUtils::initialIndex(state.electricityTransportDirForward, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
        digitalWrite(STREET_LED_PIN, LOW);
        state.streetLightOn = false;
    }
}

// ---- Information LEDs
void updateInformationLEDs(SystemState &state, Timers &timers) {
    setPixelSafe(state, WIND_INFO_LED, state.windOn ? CRGB::Red : CRGB::Black);
    setPixelSafe(state, HYDROGEN_PRODUCTION_INFO_LED, state.hydrogenProductionOn ? CRGB::Red : CRGB::Black);
    setPixelSafe(state, ELECTROLYSER_INFO_LED, state.electrolyserOn ? CRGB::Red : CRGB::Black);
    setPixelSafe(state, HYDROGEN_STORAGE_INFO_LED, state.hydrogenStorageOn ? CRGB::Red : CRGB::Black);
    setPixelSafe(state, HYDROGEN_CONSUMPTION_INFO_LED, state.h2ConsumptionOn ? CRGB::Red : CRGB::Black);
    setPixelSafe(state, ELECTRICITY_TRANSPORT_INFO_LED, state.electricityTransportOn ? CRGB::Red : CRGB::Black);
    setPixelSafe(state, STREET_LED, state.streetLightOn ? CRGB::Red : CRGB::Black);
}
