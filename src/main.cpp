#include <Arduino.h>
#include "FastLED.h"
#include "Config.h"
#include "fadeLeds.h"
#include "Hardware.h"
#include "LEDs.h"
#include "effects/Effects.h"
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
    // ensure runtime index uses the possibly overridden start
    state.windSegment = state.windSegmentStart;
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

    // reset indices
    state.windSegment = state.windSegmentStart;
    state.solarSegment = SOLAR_LED_END;
    state.electricityProductionSegment = ELECTRICITY_PRODUCTION_LED_START;
    state.hydrogenTransportSegment = HYDROGEN_TRANSPORT_LED_START;
    state.hydrogenProductionSegment = HYDROGEN_PRODUCTION_LED_START;
    state.hydrogenStorageSegment1 = HYDROGEN_STORAGE1_LED_START;
    state.hydrogenStorageSegment2 = HYDROGEN_STORAGE2_LED_START;
    state.h2ConsumptionSegment = HYDROGEN_CONSUMPTION_LED_START;
    state.electricityTransportSegment = ELECTRICITY_TRANSPORT_LED_START;
    state.storageTransportSegment = STORAGE_TRANSPORT_LED_START;
    state.storagePowerstationSegment = STORAGE_POWERSTATION_LED_START;

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
    static bool firstRun = true;
    
    // Clear all LEDs on first entry to test mode
    if (firstRun) {
        fill_solid(state.leds, NUM_LEDS, CRGB::Black);
        state.testSegmentIndex = state.testSegmentStart;
        firstRun = false;
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
        
        // Move to next LED
        state.testSegmentIndex++;
        if (state.testSegmentIndex > state.testSegmentEnd) {
            state.testSegmentIndex = state.testSegmentStart;
        }
    }
    
    // Reset firstRun when exiting test mode
    if (!state.testMode) {
        firstRun = true;
        fill_solid(state.leds, NUM_LEDS, CRGB::Black);
        resetAllVariables();
    }
}