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
        state.windSegment = EffectUtils::runSegmentDir(
            state,
            state.windSegmentStart,
            state.windSegmentEnd,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.windSegment,
            timers.previousMillisWind,
            state.firstRunWind,
            state.windDirForward
        );
        state.solarSegment = EffectUtils::runSegmentDir(
            state,
            state.solarSegmentStart,
            state.solarSegmentEnd,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.solarSegment,
            timers.previousMillisSolar,
            state.firstRunSolar,
            state.solarDirForward
        );

        if (state.windSegment == EffectUtils::terminalBound(state.windDirForward, state.windSegmentStart, state.windSegmentEnd) ||
            state.solarSegment == EffectUtils::terminalBound(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd)) {
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
    if (state.electricityProductionOn) {
        state.electricityProductionSegment = EffectUtils::runSegmentDir(
            state,
            state.electricityProductionSegmentStart,
            state.electricityProductionSegmentEnd,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.electricityProductionSegment,
            timers.previousMillisElectricityProduction,
            state.firstRunElectricityProduction,
            state.electricityProductionDirForward
        );

        if (state.electricityProductionSegment == EffectUtils::terminalBound(state.electricityProductionDirForward, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd)) {
            if (!state.electrolyserOn) {
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
    if (state.electrolyserOn) {
        if (millis() - timers.previousMillisElectrolyser >= HYDROGEN_PRODUCTION_DELAY_MS) {
            state.hydrogenProductionOn = true;
        }
    } else {
        state.hydrogenProductionOn = false;
    }
}

// ---- Hydrogen production/transport/storage/consumption (moved here)
void updateHydrogenProductionEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenProductionOn) {
        if (state.fadeEffect) {
            state.fadeEffect->update(state.leds, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd, HYDROGEN_PRODUCTION_COLOR_ACTIVE, state.firstRunHydrogenProduction);
        }
        state.hydrogenTransportOn = true;
    } else {
        EffectUtils::clearRange(state, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd);
        state.firstRunHydrogenProduction = true;
        state.hydrogenTransportOn = false;
    }
}

void updateHydrogenTransportEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenTransportOn) {
        state.hydrogenTransportSegment = EffectUtils::runSegmentDir(
            state,
            state.hydrogenTransportSegmentStart,
            state.hydrogenTransportSegmentEnd,
            HYDROGEN_PRODUCTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_PRODUCTION_COLOR_ACTIVE.r / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.g / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.hydrogenTransportSegment,
            timers.previousMillisHydrogenTransport,
            state.firstRunHydrogenTransport,
            state.hydrogenTransportDirForward
        );

        if (state.hydrogenTransportSegment == HYDROGEN_TRANSPORT_LED_MID) {
            state.h2ConsumptionOn = true;
        }
        if (state.hydrogenTransportSegment == EffectUtils::terminalBound(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd)) {
            state.hydrogenStorageOn = true;
            state.emptyPipe = true;
        }
    } else if (state.hydrogenStorageFull) {
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
            state.hydrogenTransportSegment = EffectUtils::runSegmentDir(
                state,
                state.hydrogenTransportSegmentStart,
                state.hydrogenTransportSegmentEnd,
                HYDROGEN_PRODUCTION_COLOR_ACTIVE,
                CRGB::Black,
                LED_DELAY,
                state.hydrogenTransportSegment,
                timers.previousMillisHydrogenTransport,
                state.firstRunHydrogenTransport,
                state.hydrogenTransportDirForward
            );
        } else {
            EffectUtils::clearRange(state, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
        }

        state.hydrogenStorageOn = false;
    } else {
        // reset
        state.firstRunHydrogenTransport = true;
        state.hydrogenTransportSegment = EffectUtils::initialIndex(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
        state.hydrogenStorageOn = false;
        state.emptyPipe = false;
        state.pipeEmpty = false;
    }
}

void updateHydrogenStorageEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenStorageOn) {
        state.hydrogenStorageSegment1 = EffectUtils::runSegmentDir(
            state,
            state.hydrogenStorage1SegmentStart,
            state.hydrogenStorage1SegmentEnd,
            HYDROGEN_STORAGE_COLOR_ACTIVE,
            CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.hydrogenStorageSegment1,
            timers.previousMillisHydrogenStorage,
            state.firstRunHydrogenStorage,
            state.hydrogenStorage1DirForward
        );
        state.hydrogenStorageSegment2 = EffectUtils::runSegmentDir(
            state,
            state.hydrogenStorage2SegmentStart,
            state.hydrogenStorage2SegmentEnd,
            HYDROGEN_STORAGE_COLOR_ACTIVE,
            CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.hydrogenStorageSegment2,
            timers.previousMillisHydrogenStorage2,
            state.firstRunHydrogenStorage2,
            state.hydrogenStorage2DirForward
        );

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
            state.hydrogenStorageSegment1 = EffectUtils::runSegmentDir(
                state,
                state.hydrogenStorage1SegmentStart,
                state.hydrogenStorage1SegmentEnd,
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                LED_DELAY,
                state.hydrogenStorageSegment1,
                timers.previousMillisHydrogenStorage,
                state.firstRunHydrogenStorage,
                !state.hydrogenStorage1DirForward
            );
            state.hydrogenStorageSegment2 = EffectUtils::runSegmentDir(
                state,
                state.hydrogenStorage2SegmentStart,
                state.hydrogenStorage2SegmentEnd,
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                LED_DELAY,
                state.hydrogenStorageSegment2,
                timers.previousMillisHydrogenStorage2,
                state.firstRunHydrogenStorage2,
                !state.hydrogenStorage2DirForward
            );
        }
        if (state.hydrogenStorageSegment1 == EffectUtils::initialIndex(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd) ||
            state.hydrogenStorageSegment2 == EffectUtils::initialIndex(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd)) {
            state.storageTransportOn = true;
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
    if (state.h2ConsumptionOn) {
        state.h2ConsumptionSegment = EffectUtils::runSegmentDir(
            state,
            state.hydrogenConsumptionSegmentStart,
            state.hydrogenConsumptionSegmentEnd,
            HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.h2ConsumptionSegment,
            timers.previousMillisH2Consumption,
            state.firstRunH2Consumption,
            state.h2ConsumptionDirForward
        );

        if (state.h2ConsumptionSegment == EffectUtils::terminalBound(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd)) {
            state.fabricationOn = true;
        }
    } else if (state.storageTransportOn) {
        if (state.storageTransportSegment == state.storageTransportSegmentEnd) {
            state.fabricationOn = true;
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
    if (state.fabricationOn) {
        fireEffect(state.leds, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    } else {
    EffectUtils::clearRange(state, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    }
}

// ---- Storage transport / powerstation
void updateStorageTransportEffect(SystemState &state, Timers &timers) {
    if (state.storageTransportOn) {
        state.storageTransportSegment = EffectUtils::runSegmentDir(
            state,
            state.storageTransportSegmentStart,
            state.storageTransportSegmentEnd,
            HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
            LED_DELAY2,
            state.storageTransportSegment,
            timers.previousMillisStorageTransport,
            state.firstRunStorageTransport,
            state.storageTransportDirForward
        );
        if (state.storageTransportSegment == EffectUtils::terminalBound(state.storageTransportDirForward, state.storageTransportSegmentStart, state.storageTransportSegmentEnd)) {
            state.storagePowerstationOn = true;
        }
        if (state.storagePowerstationOn) {
            state.storagePowerstationSegment = EffectUtils::runSegmentDir(
                state,
                state.storagePowerstationSegmentStart,
                state.storagePowerstationSegmentEnd,
                HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
                CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
                LED_DELAY2,
                state.storagePowerstationSegment,
                timers.previousMillisStoragePowerstation,
                state.firstRunStoragePowerstation,
                state.storagePowerstationDirForward
            );
        }
        if (state.storagePowerstationSegment == EffectUtils::terminalBound(state.storagePowerstationDirForward, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd)) {
            state.electricityTransportOn = true;
            Serial.println("Electricity transport enabled");
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
    if (state.electricityTransportOn) {
        state.electricityTransportSegment = EffectUtils::runSegmentDir(
            state,
            state.electricityTransportSegmentStart,
            state.electricityTransportSegmentEnd,
            ELECTRICITY_TRANSPORT_COLOR_ACTIVE,
            CRGB(ELECTRICITY_TRANSPORT_COLOR_ACTIVE.r / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.g / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.electricityTransportSegment,
            timers.previousMillisElectricityTransport,
            state.firstRunElectricityTransport,
            state.electricityTransportDirForward
        );

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
