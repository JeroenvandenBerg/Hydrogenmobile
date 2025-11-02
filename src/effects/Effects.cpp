#include "../../include/effects/Effects.h"
#include "../../include/Config.h"
#include "../../include/LEDs.h"
#include "../../lib/fadeLeds/fadeLeds.h"
#include "../../lib/runningLed/runningLed.h"
#include "../../lib/fireEffect/fireEffect.h"
#include "../../include/SystemState.h"
#include <Arduino.h>

// fadeEffect is owned by the runtime SystemState (state.fadeEffect)

// ---- Wind effect
void updateWindEffect(SystemState &state, Timers &timers) {
    if (state.windOn) {
        state.windSegment = runningLeds(
            state.leds,
            WIND_LED_START,
            WIND_LED_END,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.windSegment,
            timers.previousMillisWind,
            state.firstRunWind
        );
        state.solarSegment = reverseRunningLeds(
            state.leds,
            SOLAR_LED_START,
            SOLAR_LED_END,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.solarSegment,
            timers.previousMillisSolar,
            state.firstRunSolar
        );

        if (state.windSegment == WIND_LED_END || state.solarSegment == SOLAR_LED_START) {
            state.electricityProductionOn = true;
        }
    } else {
    clearSegment(state, WIND_LED_START, WIND_LED_END);
        state.firstRunWind = true;
        state.windSegment = WIND_LED_START;

    clearSegment(state, SOLAR_LED_START, SOLAR_LED_END);
        state.firstRunSolar = true;
        state.solarSegment = SOLAR_LED_END;
        state.electricityProductionOn = false;
    }
}

// ---- Electricity production effect
void updateElectricityProductionEffect(SystemState &state, Timers &timers) {
    if (state.electricityProductionOn) {
        state.electricityProductionSegment = runningLeds(
            state.leds,
            ELECTRICITY_PRODUCTION_LED_START,
            ELECTRICITY_PRODUCTION_LED_END,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.electricityProductionSegment,
            timers.previousMillisElectricityProduction,
            state.firstRunElectricityProduction
        );

        if (state.electricityProductionSegment == ELECTRICITY_PRODUCTION_LED_END) {
            if (!state.electrolyserOn) {
                state.electrolyserOn = true;
                timers.previousMillisElectrolyser = millis();
            }
        }
    } else {
        clearSegment(state, ELECTRICITY_PRODUCTION_LED_START, ELECTRICITY_PRODUCTION_LED_END);
        state.firstRunElectricityProduction = true;
        state.electricityProductionSegment = ELECTRICITY_PRODUCTION_LED_START;
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
            state.fadeEffect->update(state.leds, HYDROGEN_PRODUCTION_LED_START, HYDROGEN_PRODUCTION_LED_END, HYDROGEN_PRODUCTION_COLOR_ACTIVE, state.firstRunHydrogenProduction);
        }
        state.hydrogenTransportOn = true;
    } else {
    clearSegment(state, HYDROGEN_PRODUCTION_LED_START, HYDROGEN_PRODUCTION_LED_END);
        state.firstRunHydrogenProduction = true;
        state.hydrogenTransportOn = false;
    }
}

void updateHydrogenTransportEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenTransportOn) {
        state.hydrogenTransportSegment = runningLeds(
            state.leds,
            HYDROGEN_TRANSPORT_LED_START,
            HYDROGEN_TRANSPORT_LED_END,
            HYDROGEN_PRODUCTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_PRODUCTION_COLOR_ACTIVE.r / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.g / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.hydrogenTransportSegment,
            timers.previousMillisHydrogenTransport,
            state.firstRunHydrogenTransport
        );

        if (state.hydrogenTransportSegment == HYDROGEN_TRANSPORT_LED_MID) {
            state.h2ConsumptionOn = true;
        }
        if (state.hydrogenTransportSegment == HYDROGEN_TRANSPORT_LED_END) {
            state.hydrogenStorageOn = true;
            state.emptyPipe = true;
        }
    } else if (state.hydrogenStorageFull) {
        if (state.hydrogenTransportSegment == HYDROGEN_TRANSPORT_LED_START) {
            state.pipeEmpty = true;
        }

        if (state.emptyPipe) {
            fill_solid(state.leds + HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END - HYDROGEN_TRANSPORT_LED_START + 1,
                       CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 20, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 20, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 20));
            state.hydrogenTransportSegment = HYDROGEN_TRANSPORT_LED_START;
            state.emptyPipe = false;
        }

        if (!state.pipeEmpty) {
            state.hydrogenTransportSegment = runningLeds(
                state.leds,
                HYDROGEN_TRANSPORT_LED_START,
                HYDROGEN_TRANSPORT_LED_END,
                HYDROGEN_PRODUCTION_COLOR_ACTIVE,
                CRGB::Black,
                LED_DELAY,
                state.hydrogenTransportSegment,
                timers.previousMillisHydrogenTransport,
                state.firstRunHydrogenTransport
            );
        } else {
            clearSegment(state, HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END);
        }

        state.hydrogenStorageOn = false;
    } else {
        // reset
        state.firstRunHydrogenTransport = true;
        state.hydrogenTransportSegment = HYDROGEN_TRANSPORT_LED_START;
        state.hydrogenStorageOn = false;
        state.emptyPipe = false;
        state.pipeEmpty = false;
    }
}

void updateHydrogenStorageEffect(SystemState &state, Timers &timers) {
    if (state.hydrogenStorageOn) {
        state.hydrogenStorageSegment1 = runningLeds(
            state.leds,
            HYDROGEN_STORAGE1_LED_START,
            HYDROGEN_STORAGE1_LED_END,
            HYDROGEN_STORAGE_COLOR_ACTIVE,
            CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.hydrogenStorageSegment1,
            timers.previousMillisHydrogenStorage,
            state.firstRunHydrogenStorage
        );
        state.hydrogenStorageSegment2 = runningLeds(
            state.leds,
            HYDROGEN_STORAGE2_LED_START,
            HYDROGEN_STORAGE2_LED_END,
            HYDROGEN_STORAGE_COLOR_ACTIVE,
            CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.hydrogenStorageSegment2,
            timers.previousMillisHydrogenStorage2,
            state.firstRunHydrogenStorage2
        );

        if (state.hydrogenStorageSegment1 == HYDROGEN_STORAGE1_LED_END) {
            state.hydrogenStorageFull = true;
        }
    } else if (state.hydrogenStorageFull) {
        if (!state.storageTimerStarted) {
            state.h2ConsumptionOn = false;
            fill_solid(state.leds + HYDROGEN_STORAGE1_LED_START, HYDROGEN_STORAGE1_LED_END - HYDROGEN_STORAGE1_LED_START + 1, CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10));
            fill_solid(state.leds + HYDROGEN_STORAGE2_LED_START, HYDROGEN_STORAGE2_LED_END - HYDROGEN_STORAGE2_LED_START + 1, CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10));
            state.h2ConsumptionSegment = HYDROGEN_CONSUMPTION_LED_START;
            state.hydrogenStorageSegment1 = HYDROGEN_STORAGE1_LED_END;
            state.hydrogenStorageSegment2 = HYDROGEN_STORAGE2_LED_END;
            timers.hydrogenStorageFullTimer = millis();
            state.storageTimerStarted = true;
        }
        if (millis() - timers.hydrogenStorageFullTimer >= HYDROGEN_STORAGE_DELAY_MS) {
            state.hydrogenStorageSegment1 = reverseRunningLeds(
                state.leds,
                HYDROGEN_STORAGE1_LED_START,
                HYDROGEN_STORAGE1_LED_END,
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                LED_DELAY,
                state.hydrogenStorageSegment1,
                timers.previousMillisHydrogenStorage,
                state.firstRunHydrogenStorage
            );
            state.hydrogenStorageSegment2 = reverseRunningLeds(
                state.leds,
                HYDROGEN_STORAGE2_LED_START,
                HYDROGEN_STORAGE2_LED_END,
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                LED_DELAY,
                state.hydrogenStorageSegment2,
                timers.previousMillisHydrogenStorage2,
                state.firstRunHydrogenStorage2
            );
        }
        if (state.hydrogenStorageSegment1 == HYDROGEN_STORAGE1_LED_START || state.hydrogenStorageSegment2 == HYDROGEN_STORAGE2_LED_START) {
            state.storageTransportOn = true;
        }
    } else {
    clearSegment(state, HYDROGEN_STORAGE1_LED_START, HYDROGEN_STORAGE1_LED_END);
    clearSegment(state, HYDROGEN_STORAGE2_LED_START, HYDROGEN_STORAGE2_LED_END);
        state.firstRunHydrogenStorage = true;
        state.firstRunHydrogenStorage2 = true;
        state.hydrogenStorageSegment1 = HYDROGEN_STORAGE1_LED_START;
        state.hydrogenStorageSegment2 = HYDROGEN_STORAGE2_LED_START;
        state.storageTransportOn = false;
        state.storageTimerStarted = false;
    }
}

void updateH2ConsumptionEffect(SystemState &state, Timers &timers) {
    if (state.h2ConsumptionOn) {
        state.h2ConsumptionSegment = runningLeds(
            state.leds,
            HYDROGEN_CONSUMPTION_LED_START,
            HYDROGEN_CONSUMPTION_LED_END,
            HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.h2ConsumptionSegment,
            timers.previousMillisH2Consumption,
            state.firstRunH2Consumption
        );

        if (state.h2ConsumptionSegment == HYDROGEN_CONSUMPTION_LED_END) {
            state.fabricationOn = true;
        }
    } else if (state.storageTransportOn) {
        if (state.storageTransportSegment == STORAGE_TRANSPORT_LED_END) {
            state.fabricationOn = true;
        }
    } else {
    clearSegment(state, HYDROGEN_CONSUMPTION_LED_START, HYDROGEN_CONSUMPTION_LED_END);
        state.firstRunH2Consumption = true;
        state.h2ConsumptionSegment = HYDROGEN_CONSUMPTION_LED_START;
        state.fabricationOn = false;
    }
}

// ---- Fabrication effect
void updateFabricationEffect(SystemState &state, Timers &timers) {
    if (state.fabricationOn) {
        fireEffect(state.leds, FABRICATION_LED_START, FABRICATION_LED_END);
    } else {
        clearSegment(state, FABRICATION_LED_START, FABRICATION_LED_END);
    }
}

// ---- Storage transport / powerstation
void updateStorageTransportEffect(SystemState &state, Timers &timers) {
    if (state.storageTransportOn) {
        state.storageTransportSegment = runningLeds(
            state.leds,
            STORAGE_TRANSPORT_LED_START,
            STORAGE_TRANSPORT_LED_END,
            HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
            LED_DELAY2,
            state.storageTransportSegment,
            timers.previousMillisStorageTransport,
            state.firstRunStorageTransport
        );
        if (state.storageTransportSegment == STORAGE_TRANSPORT_LED_END) {
            state.storagePowerstationOn = true;
        }
        if (state.storagePowerstationOn) {
            state.storagePowerstationSegment = runningLeds(
                state.leds,
                STORAGE_POWERSTATION_LED_START,
                STORAGE_POWERSTATION_LED_END,
                HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
                CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
                LED_DELAY2,
                state.storagePowerstationSegment,
                timers.previousMillisStoragePowerstation,
                state.firstRunStoragePowerstation
            );
        }
        if (state.storagePowerstationSegment == STORAGE_POWERSTATION_LED_END) {
            state.electricityTransportOn = true;
            Serial.println("Electricity transport enabled");
        }
    } else {
        clearSegment(state, STORAGE_TRANSPORT_LED_START, STORAGE_TRANSPORT_LED_END);
        state.firstRunStorageTransport = true;
        state.storageTransportSegment = STORAGE_TRANSPORT_LED_START;
        clearSegment(state, STORAGE_POWERSTATION_LED_START, STORAGE_POWERSTATION_LED_END);
        state.firstRunStoragePowerstation = true;
        state.storagePowerstationSegment = STORAGE_POWERSTATION_LED_START;
        state.storagePowerstationOn = false;
    }
}

// ---- Electricity transport
void updateElectricityEffect(SystemState &state, Timers &timers) {
    if (state.electricityTransportOn) {
        state.electricityTransportSegment = runningLeds(
            state.leds,
            ELECTRICITY_TRANSPORT_LED_START,
            ELECTRICITY_TRANSPORT_LED_END,
            ELECTRICITY_TRANSPORT_COLOR_ACTIVE,
            CRGB(ELECTRICITY_TRANSPORT_COLOR_ACTIVE.r / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.g / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.b / 10),
            LED_DELAY,
            state.electricityTransportSegment,
            timers.previousMillisElectricityTransport,
            state.firstRunElectricityTransport
        );

        if (state.electricityTransportSegment == ELECTRICITY_TRANSPORT_LED_END) {
            digitalWrite(STREET_LED_PIN, HIGH);
            state.streetLightOn = true;
        }
    } else {
    clearSegment(state, ELECTRICITY_TRANSPORT_LED_START, ELECTRICITY_TRANSPORT_LED_END);
        state.firstRunElectricityTransport = true;
        state.electricityTransportSegment = ELECTRICITY_TRANSPORT_LED_START;
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
