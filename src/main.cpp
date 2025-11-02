#include <Arduino.h>
#include "FastLED.h"
#include "Config.h"
#include "fadeLeds.h"
#include "Hardware.h"
#include "LEDs.h"
#include "effects/Effects.h"
#include "effects/EffectUtils.h"
#include "SystemState.h"
#include "WebServerSafe.h"

// ========================== Global state ==========================
// LED buffer moved into SystemState (state.leds)

// Centralized state and timers (replaces many loose globals)
SystemState state;
Timers timers;

// ========================== Helpers ==========================
// LED helpers moved to `src/utils/LEDs.cpp` (declared in include/LEDs.h)

// ========================== Declarations ==========================
void updateSegments();
void updateRelays();
void checkButtonState();
void resetAllVariables();
void runTestMode();

// ========================== Setup & Loop ==========================
void setup() {
    Serial.begin(115200);
    hardwareInit(state);
    // run a quick LED test (chase from 0..NUM_LEDS-1) so we can verify wiring
    testAllLeds(state, 20);
    // allocate and initialize fadeEffect owned by the state
    state.fadeEffect = new fadeLeds(2000);
    digitalWrite(BUTTON_LED_PIN, HIGH);
    resetAllVariables();
    // start the safe web UI which will load any persisted wind segment overrides
    initWebServerSafe();
    // ensure runtime index uses the possibly overridden start and configured direction
    state.windSegment = EffectUtils::initialIndex(state.windDirForward, state.windSegmentStart, state.windSegmentEnd);
    state.windOn = true;
}

void loop() {
    if (state.testMode) {
        runTestMode();
    } else {
        checkButtonState();
        updateSegments();
        updateRelays();
    }
    FastLED.show();
}

// ========================== Implementations ==========================
// hardware initialization moved to src/Hardware.cpp (hardwareInit)

void updateSegments() {
    updateWindEffect(state, timers);
    updateElectricityProductionEffect(state, timers);
    updateElectrolyserEffect(state, timers);
    updateHydrogenProductionEffect(state, timers);
    updateHydrogenTransportEffect(state, timers);
    updateHydrogenStorageEffect(state, timers);
    updateH2ConsumptionEffect(state, timers);
    updateFabricationEffect(state, timers);
    updateElectricityEffect(state, timers);
    updateStorageTransportEffect(state, timers);
    // Update the small information LEDs (status indicators)
    updateInformationLEDs(state, timers);
}

void updateRelays() {
    digitalWrite(WIND_TURBINE_RELAY_PIN, state.windOn ? HIGH : LOW);
    digitalWrite(ELECTROLYSER_RELAY_PIN, state.electrolyserOn ? HIGH : LOW);
}

// Effect implementations are provided in src/effects/Effects.cpp

void checkButtonState() {
    uint32_t currentMillis = millis();
    // if general timer active handle timeouts
    if (state.generalTimerActive) {
        if (currentMillis - timers.generalTimerStartTime >= WIND_TIME_MS && state.windOn) {
            state.windOn = false;
        }

        if (currentMillis - timers.generalTimerStartTime >= RUN_TIME_MS) {
            state.hydrogenStorageFull = false;
            state.electricityTransportOn = false;
            state.generalTimerActive = false;
            state.buttonDisabled = false;
            state.emptyPipe = false;
            state.pipeEmpty = false;
            resetAllVariables();
            digitalWrite(BUTTON_LED_PIN, HIGH);
        }
        return;
    }

    // Debounced button check
    if (currentMillis - timers.previousButtonCheckMillis >= BUTTON_CHECK_INTERVAL) {
        timers.previousButtonCheckMillis = currentMillis;

        static uint32_t lastPressTime = 0;
        const uint32_t debounceMs = 50;

        if (digitalRead(BUTTON_PIN) == LOW && !state.buttonDisabled) {
            if (currentMillis - lastPressTime < debounceMs) return;
            lastPressTime = currentMillis;

            digitalWrite(BUTTON_LED_PIN, LOW);
            state.windOn = true;
            state.buttonDisabled = true;
            state.generalTimerActive = true;
            timers.generalTimerStartTime = currentMillis;
        }
    }
}

void resetAllVariables() {
    // reset flags (state)
    state.windOn = false;
    state.solarOn = false;
    state.electricityProductionOn = false;
    state.electrolyserOn = false;
    state.hydrogenTransportOn = false;
    state.hydrogenProductionOn = false;
    state.hydrogenStorageOn = false;
    state.hydrogenStorageFull = false;
    state.h2ConsumptionOn = false;
    state.fabricationOn = false;
    state.electricityTransportOn = false;
    state.storageTransportOn = false;
    state.storagePowerstationOn = false;
    state.streetLightOn = false;
    state.emptyPipe = false;
    state.pipeEmpty = false;

    // reset timers to now (avoid immediate re-trigger)
    uint32_t now = millis();
    timers.previousButtonCheckMillis = now;
    timers.previousMillisWind = now;
    timers.previousMillisSolar = now;
    timers.previousMillisElectricityProduction = now;
    timers.previousMillisElectrolyser = now;
    timers.previousMillisHydrogenTransport = now;
    timers.previousMillisHydrogenProduction = now;
    timers.previousMillisHydrogenStorage = now;
    timers.previousMillisHydrogenStorage2 = now;
    timers.previousMillisH2Consumption = now;
    timers.hydrogenStorageFullStartTime = now;
    timers.previousMillisElectricityTransport = now;
    timers.previousMillisStorageTransport = now;
    timers.previousMillisStoragePowerstation = now;
    timers.hydrogenStorageFullTimer = now;

    // reset indices to initial positions based on configured directions and ranges
    state.windSegment = EffectUtils::initialIndex(state.windDirForward, state.windSegmentStart, state.windSegmentEnd);
    state.solarSegment = EffectUtils::initialIndex(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd);
    state.electricityProductionSegment = EffectUtils::initialIndex(state.electricityProductionDirForward, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd);
    state.hydrogenTransportSegment = EffectUtils::initialIndex(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
    state.hydrogenProductionSegment = state.hydrogenProductionSegmentStart; // fade effect uses range only
    state.hydrogenStorageSegment1 = EffectUtils::initialIndex(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
    state.hydrogenStorageSegment2 = EffectUtils::initialIndex(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
    state.h2ConsumptionSegment = EffectUtils::initialIndex(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
    state.electricityTransportSegment = EffectUtils::initialIndex(state.electricityTransportDirForward, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
    state.storageTransportSegment = EffectUtils::initialIndex(state.storageTransportDirForward, state.storageTransportSegmentStart, state.storageTransportSegmentEnd);
    state.storagePowerstationSegment = EffectUtils::initialIndex(state.storagePowerstationDirForward, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);

    // reset first-run flags
    state.firstRunWind = true;
    state.firstRunSolar = true;
    state.firstRunElectricityProduction = true;
    state.firstRunHydrogenProduction = true;
    state.firstRunHydrogenTransport = true;
    state.firstRunHydrogenStorage = true;
    state.firstRunHydrogenStorage2 = true;
    state.firstRunH2Consumption = true;
    state.firstRunElectricityTransport = true;
    state.firstRunStorageTransport = true;
    state.firstRunStoragePowerstation = true;

    // button/timer states
    state.buttonDisabled = false;
    timers.buttonDisableStartTime = 0;
    timers.generalTimerStartTime = 0;
    state.generalTimerActive = false;
    state.storageTimerStarted = false;
}

void runTestMode() {
    static uint32_t previousMillis = 0;

    // Initialize when entering test (sentinel set by /test handler)
    if (state.testSegmentIndex < state.testSegmentStart || state.testSegmentIndex > state.testSegmentEnd) {
        state.testSegmentIndex = state.testDirForward ? state.testSegmentStart : state.testSegmentEnd;
        previousMillis = millis();
    }

    // Run a simple running LED effect on the test segment
    if (millis() - previousMillis >= LED_DELAY) {
        previousMillis = millis();

        // Clear the segment
        for (int i = state.testSegmentStart; i <= state.testSegmentEnd; i++) {
            state.leds[i] = CRGB::Black;
        }

        // Light up current position
        state.leds[state.testSegmentIndex] = CRGB::White;

        // Move to next LED based on direction
        if (state.testDirForward) {
            state.testSegmentIndex++;
            if (state.testSegmentIndex > state.testSegmentEnd) {
                state.testSegmentIndex = state.testSegmentStart;
            }
        } else {
            state.testSegmentIndex--;
            if (state.testSegmentIndex < state.testSegmentStart) {
                state.testSegmentIndex = state.testSegmentEnd;
            }
        }
    }
}