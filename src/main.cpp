#include <Arduino.h>
#include "FastLED.h"
#include "Config.h"
#include "fadeLeds.h"
#include "fireEffect.h"
#include "Hardware.h"
#include "LEDs.h"
#include "effects/Effects.h"
#include "effects/EffectUtils.h"
#include "SystemState.h"
#include "WebServerSafe.h"
#include "soc/rtc_cntl_reg.h"

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
void startProgramAuto();

// ========================== Setup & Loop ==========================
void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(DIAG_CPU_FREQ_MHZ);
    // Load persisted settings first so hardware init uses the configured LED count
    initWebServerSafe();
    hardwareInit(state);
    // allocate and initialize fadeEffect owned by the state
    state.fadeEffect = new fadeLeds(2000);
    // Do not auto-start LED test mode on boot; this can trigger brownout on weak supplies.
    state.testMode = false;
    state.testSegmentStart = 0;
    state.testSegmentEnd = state.totalLeds > 0 ? state.totalLeds - 1 : 0;
    state.testSegmentIndex = -1;
    state.testDirForward = true;
    state.testEffectType = 0;
    state.testColor = CRGB::White;
    state.testDelay = 5;
    state.testPhase = 0;
    state.testPhaseStartTime = millis();
    digitalWrite(state.buttonLedPin, HIGH);
    resetAllVariables();
    // ensure runtime index uses the possibly overridden start and configured direction
    state.windSegment = EffectUtils::initialIndex(state.windDirForward, state.windSegmentStart, state.windSegmentEnd);
}

void loop() {
    if (state.autoStartEnabled && !state.autoStartTriggered && !state.generalTimerActive && !state.testMode) {
        startProgramAuto();
    }

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
    // Strict simplified runtime: render only Wind and Solar using web-configured ranges and delays.
    // Do not clear every frame; the running effect needs previous frame state for dim trailing.
    if (!state.windOn && !state.solarOn) {
        if (state.totalLeds > 0) {
            fill_solid(state.leds, state.totalLeds, CRGB::Black);
        }
        updateInformationLEDs(state, timers);
        return;
    }

    int prevWindIndex = state.windSegment;
    bool prevWindFirstRun = state.firstRunWind;

    if (state.windOn && state.windEnabled) {
        state.windSegment = EffectUtils::runSegmentDir(
            state,
            state.windSegmentStart,
            state.windSegmentEnd,
            state.windColor,
            CRGB(state.windColor.r / 10, state.windColor.g / 10, state.windColor.b / 10),
            state.windDelay,
            state.windSegment,
            timers.previousMillisWind,
            state.firstRunWind,
            state.windDirForward
        );

        bool windReachedTerminal = !prevWindFirstRun &&
            (prevWindIndex == EffectUtils::terminalBound(state.windDirForward, state.windSegmentStart, state.windSegmentEnd));
        if (windReachedTerminal && state.solarEnabled) {
            state.solarOn = true;
        }
    } else {
        state.firstRunWind = true;
        state.windSegment = EffectUtils::initialIndex(state.windDirForward, state.windSegmentStart, state.windSegmentEnd);
    }

    if (state.solarOn && state.solarEnabled) {
        state.solarSegment = EffectUtils::runSegmentDir(
            state,
            state.solarSegmentStart,
            state.solarSegmentEnd,
            state.solarColor,
            CRGB(state.solarColor.r / 10, state.solarColor.g / 10, state.solarColor.b / 10),
            state.solarDelay,
            state.solarSegment,
            timers.previousMillisSolar,
            state.firstRunSolar,
            state.solarDirForward
        );
    } else {
        state.firstRunSolar = true;
        state.solarSegment = EffectUtils::initialIndex(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd);
    }

    // Disable all other chain states in this mode.
    state.electricityProductionOn = false;
    state.electrolyserOn = false;
    state.hydrogenTransportOn = false;
    state.hydrogenTransportDelayActive = false;
    state.hydrogenProductionOn = false;
    state.hydrogenStorageOn = false;
    state.h2ConsumptionOn = false;
    state.fabricationOn = false;
    state.electricityTransportOn = false;
    state.storageTransportOn = false;
    state.storagePowerstationOn = false;
    state.streetLightOn = false;

    updateInformationLEDs(state, timers);
}

void updateRelays() {
    setRelayWind(state.windOn);
    setRelayElectrolyser(state.electrolyserOn);
}

// Effect implementations are provided in src/effects/Effects.cpp

void checkButtonState() {
    uint32_t currentMillis = millis();
    // if general timer active handle timeouts
    if (state.generalTimerActive) {
        uint32_t windStopMs = static_cast<uint32_t>(state.windStopSeconds) * 1000UL;
        if (currentMillis - timers.generalTimerStartTime >= windStopMs) {
            state.windOn = false;
            state.solarOn = false;
        }

        if (currentMillis - timers.generalTimerStartTime >= RUN_TIME_MS) {
            state.hydrogenStorageFull = false;
            state.electricityTransportOn = false;
            state.generalTimerActive = false;
            state.buttonDisabled = false;
            state.emptyPipe = false;
            state.pipeEmpty = false;
            resetAllVariables();
            digitalWrite(state.buttonLedPin, HIGH);
        }
        return;
    }

    if (state.autoStartEnabled) {
        return;
    }

    // Debounced button check
    if (currentMillis - timers.previousButtonCheckMillis >= BUTTON_CHECK_INTERVAL) {
        timers.previousButtonCheckMillis = currentMillis;

        static uint32_t lastPressTime = 0;
        const uint32_t debounceMs = 50;

        if (digitalRead(state.buttonPin) == LOW && !state.buttonDisabled) {
            if (currentMillis - lastPressTime < debounceMs) return;
            lastPressTime = currentMillis;

            if (state.totalLeds > 0) {
                fill_solid(state.leds, state.totalLeds, CRGB::Black);
            }
            digitalWrite(state.buttonLedPin, LOW);
            state.windOn = true;
            state.solarOn = false;
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
    state.hydrogenTransportDelayActive = false;
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
    state.autoStartTriggered = false;

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

void startProgramAuto() {
    if (state.totalLeds > 0) {
        fill_solid(state.leds, state.totalLeds, CRGB::Black);
    }
    state.autoStartTriggered = true;
    state.buttonDisabled = true;
    state.generalTimerActive = true;
    timers.generalTimerStartTime = millis();
    state.windOn = true;
    state.solarOn = false;
    digitalWrite(state.buttonLedPin, LOW);
}

void runTestMode() {
    static uint32_t previousMillis = 0;
    static bool firstRunTest = true;

    // Initialize when entering test (sentinel set by /test handler)
    if (state.testSegmentIndex < state.testSegmentStart || state.testSegmentIndex > state.testSegmentEnd) {
        state.testSegmentIndex = state.testDirForward ? state.testSegmentStart : state.testSegmentEnd;
        previousMillis = millis();
        firstRunTest = true;
        state.testPhase = 0;
        state.testPhaseStartTime = millis();
    }

    // Phase 0: LED Check - run a simple white LED sequence for a short time
    if (state.testPhase == 0) {
        if (millis() - state.testPhaseStartTime >= 2000) {
            // Move to effect demo phase
            state.testPhase = 1;
            state.testPhaseStartTime = millis();
            state.testSegmentIndex = state.testDirForward ? state.testSegmentStart : state.testSegmentEnd;
            firstRunTest = true;
            // Clear for next phase
            for (int i = state.testSegmentStart; i <= state.testSegmentEnd; i++) {
                state.leds[i] = CRGB::Black;
            }
            return;
        }

        // Simple running white LED to verify segment range
        if (millis() - previousMillis >= state.testDelay) { // Use configured test delay
            previousMillis = millis();

            // Clear the segment
            for (int i = state.testSegmentStart; i <= state.testSegmentEnd; i++) {
                state.leds[i] = CRGB::Black;
            }

            // Light up current position in white
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
    // Phase 1: Effect Demo - run the selected effect
    else if (state.testPhase == 1) {
        if (state.testEffectType == 1) {
            // Fire effect
            fireEffect(state.leds, state.testSegmentStart, state.testSegmentEnd);
        } else if (state.testEffectType == 2) {
            // Fade effect
            if (state.fadeEffect) {
                state.fadeEffect->update(state.leds, state.testSegmentStart, state.testSegmentEnd, state.testColor, firstRunTest, state.testDelay);
            }
            firstRunTest = false;
        } else {
            // Running effect (default)
            if (millis() - previousMillis >= state.testDelay) {
                previousMillis = millis();

                // Clear the segment
                for (int i = state.testSegmentStart; i <= state.testSegmentEnd; i++) {
                    state.leds[i] = CRGB::Black;
                }

                // Light up current position with selected color
                state.leds[state.testSegmentIndex] = state.testColor;

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
    }
}