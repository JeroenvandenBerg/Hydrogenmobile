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
void updateHydrogenFromRenewablesProgram();
void updateHydrogenFromStorageProgram();
void updateRelays();
void checkButtonState();
void resetAllVariables();
void runTestMode();
void startProgramAuto();
bool runWindStopDrainSequence();

// ========================== Setup & Loop ==========================
void setup() {
    Serial.begin(115200);
    setCpuFrequencyMhz(DIAG_CPU_FREQ_MHZ);
#if DIAG_DISABLE_BROWNOUT
    // Diagnostic-only workaround for unstable test boards.
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif
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
    // Start only via auto-start setting or manual start actions.
    updateRelays();
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
    switch (state.activeProgram) {
        case ProgramVariant::HYDROGEN_FROM_STORAGE:
            updateHydrogenFromStorageProgram();
            break;
        case ProgramVariant::HYDROGEN_FROM_RENEWABLES:
        default:
            updateHydrogenFromRenewablesProgram();
            break;
    }
}

bool runWindStopDrainSequence() {
    if (!state.emptyPipe) {
        return false;
    }

    const CRGB h2TransportDim(
        state.hydrogenTransportColor.r / 10,
        state.hydrogenTransportColor.g / 10,
        state.hydrogenTransportColor.b / 10
    );
    const CRGB storageInDim(
        state.hydrogenStorage1Color.r / 10,
        state.hydrogenStorage1Color.g / 10,
        state.hydrogenStorage1Color.b / 10
    );

    if (!state.pipeEmpty) {
        fill_solid(
            state.leds + state.hydrogenTransportSegmentStart,
            state.hydrogenTransportSegmentEnd - state.hydrogenTransportSegmentStart + 1,
            h2TransportDim
        );
        fill_solid(
            state.leds + state.hydrogenStorage1SegmentStart,
            state.hydrogenStorage1SegmentEnd - state.hydrogenStorage1SegmentStart + 1,
            storageInDim
        );

        // Start draining from the first LED in configured direction.
        state.hydrogenTransportSegment = EffectUtils::initialIndex(
            state.hydrogenTransportDirForward,
            state.hydrogenTransportSegmentStart,
            state.hydrogenTransportSegmentEnd
        );
        state.hydrogenStorageSegment1 = EffectUtils::initialIndex(
            state.hydrogenStorage1DirForward,
            state.hydrogenStorage1SegmentStart,
            state.hydrogenStorage1SegmentEnd
        );
        timers.previousMillisHydrogenTransport = millis();
        timers.previousMillisHydrogenStorage = millis();
        state.pipeEmpty = true;
    }

    // Drain Hydrogen Transport first.
    if (state.hydrogenTransportOn) {
        EffectUtils::advanceIndexDir(
            state.hydrogenTransportDelay,
            state.hydrogenTransportSegmentStart,
            state.hydrogenTransportSegmentEnd,
            state.hydrogenTransportDirForward,
            state.hydrogenTransportSegment,
            timers.previousMillisHydrogenTransport,
            state.firstRunHydrogenTransport
        );
        setPixelSafe(state, state.hydrogenTransportSegment, CRGB::Black);

        if (state.hydrogenTransportSegment ==
            EffectUtils::terminalBound(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd)) {
            state.hydrogenTransportOn = false;
            state.firstRunHydrogenTransport = true;
        }
    } else if (state.hydrogenStorageInOn) {
        // After transport is fully drained, drain Storage In.
        EffectUtils::advanceIndexDir(
            state.hydrogenStorage1Delay,
            state.hydrogenStorage1SegmentStart,
            state.hydrogenStorage1SegmentEnd,
            state.hydrogenStorage1DirForward,
            state.hydrogenStorageSegment1,
            timers.previousMillisHydrogenStorage,
            state.firstRunHydrogenStorage
        );
        setPixelSafe(state, state.hydrogenStorageSegment1, CRGB::Black);

        if (state.hydrogenStorageSegment1 ==
            EffectUtils::terminalBound(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd)) {
            state.hydrogenStorageInOn = false;
            state.hydrogenStorageOn = false;
            state.emptyPipe = false;
            state.pipeEmpty = false;
            state.firstRunHydrogenStorage = true;
        }
    }

    updateInformationLEDs(state, timers);
    return true;
}

void updateHydrogenFromRenewablesProgram() {
    // Simplified runtime: render Wind, Solar, then Hydrogen Transport using web-configured ranges and delays.
    // Do not clear every frame; the running effect needs previous frame state for dim trailing.
    if (runWindStopDrainSequence()) {
        return;
    }

    if (!state.windOn && !state.solarOn && !state.hydrogenTransportOn && !state.hydrogenStorageInOn && !state.hydrogenStorageOutOn && !state.h2ConsumptionOn) {
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

    int prevSolarIndex = state.solarSegment;
    bool prevSolarFirstRun = state.firstRunSolar;

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

        bool solarReachedTerminal = !prevSolarFirstRun &&
            (prevSolarIndex == EffectUtils::terminalBound(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd));
        // Only arm Hydrogen Transport once before downstream states become active.
        if (solarReachedTerminal && !state.hydrogenTransportDelayActive && !state.hydrogenTransportOn &&
            !state.hydrogenStorageInOn && !state.hydrogenStorageOutOn && !state.h2ConsumptionOn && !state.fabricationOn) {
            timers.previousMillisElectrolyser = millis();
            if (state.hydrogenTransportDelaySeconds == 0) {
                state.hydrogenTransportOn = state.hydrogenTransportEnabled;
                state.hydrogenTransportDelayActive = false;
            } else {
                state.hydrogenTransportOn = false;
                state.hydrogenTransportDelayActive = true;
            }
        }
    } else {
        state.firstRunSolar = true;
        state.solarSegment = EffectUtils::initialIndex(state.solarDirForward, state.solarSegmentStart, state.solarSegmentEnd);
    }

    if (state.hydrogenTransportDelayActive) {
        uint32_t elapsed = millis() - timers.previousMillisElectrolyser;
        uint32_t required = static_cast<uint32_t>(state.hydrogenTransportDelaySeconds) * 1000UL;
        if (elapsed >= required) {
            state.hydrogenTransportDelayActive = false;
            state.hydrogenTransportOn = state.hydrogenTransportEnabled;
        }
    }

    int prevHydrogenTransportIndex = state.hydrogenTransportSegment;
    bool prevHydrogenTransportFirstRun = state.firstRunHydrogenTransport;

    if (state.hydrogenTransportOn && state.hydrogenTransportEnabled) {
        state.hydrogenTransportSegment = EffectUtils::runSegmentDir(
            state,
            state.hydrogenTransportSegmentStart,
            state.hydrogenTransportSegmentEnd,
            state.hydrogenTransportColor,
            CRGB(state.hydrogenTransportColor.r / 10, state.hydrogenTransportColor.g / 10, state.hydrogenTransportColor.b / 10),
            state.hydrogenTransportDelay,
            state.hydrogenTransportSegment,
            timers.previousMillisHydrogenTransport,
            state.firstRunHydrogenTransport,
            state.hydrogenTransportDirForward
        );

        bool hydrogenTransportReachedTerminal = !prevHydrogenTransportFirstRun &&
            (prevHydrogenTransportIndex == EffectUtils::terminalBound(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd));
        if (hydrogenTransportReachedTerminal) {
            // Keep Hydrogen Transport running; only use terminal to start downstream stages.
            state.hydrogenStorageInOn = state.hydrogenStorageEnabled;
            state.hydrogenStorageOutOn = false;
            state.h2ConsumptionOn = state.h2ConsumptionEnabled;
        }
    } else {
        state.firstRunHydrogenTransport = true;
        state.hydrogenTransportSegment = EffectUtils::initialIndex(state.hydrogenTransportDirForward, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
    }

    if (state.hydrogenStorageInOn && state.hydrogenStorageEnabled) {
        state.hydrogenStorageSegment1 = EffectUtils::runSegmentDir(
            state,
            state.hydrogenStorage1SegmentStart,
            state.hydrogenStorage1SegmentEnd,
            state.hydrogenStorage1Color,
            CRGB(state.hydrogenStorage1Color.r / 10, state.hydrogenStorage1Color.g / 10, state.hydrogenStorage1Color.b / 10),
            state.hydrogenStorage1Delay,
            state.hydrogenStorageSegment1,
            timers.previousMillisHydrogenStorage,
            state.firstRunHydrogenStorage,
            state.hydrogenStorage1DirForward
        );
    } else {
        state.firstRunHydrogenStorage = true;
        state.hydrogenStorageSegment1 = EffectUtils::initialIndex(state.hydrogenStorage1DirForward, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
    }

    if (state.hydrogenStorageOutOn && state.hydrogenStorageEnabled) {
        state.hydrogenStorageSegment2 = EffectUtils::runSegmentDir(
            state,
            state.hydrogenStorage2SegmentStart,
            state.hydrogenStorage2SegmentEnd,
            state.hydrogenStorage2Color,
            CRGB(state.hydrogenStorage2Color.r / 10, state.hydrogenStorage2Color.g / 10, state.hydrogenStorage2Color.b / 10),
            state.hydrogenStorage2Delay,
            state.hydrogenStorageSegment2,
            timers.previousMillisHydrogenStorage2,
            state.firstRunHydrogenStorage2,
            state.hydrogenStorage2DirForward
        );
    } else {
        state.firstRunHydrogenStorage2 = true;
        state.hydrogenStorageSegment2 = EffectUtils::initialIndex(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
    }

    state.hydrogenStorageOn = state.hydrogenStorageInOn || state.hydrogenStorageOutOn;

    if (state.h2ConsumptionOn && state.h2ConsumptionEnabled) {
        int prevH2ConsumptionIndex = state.h2ConsumptionSegment;
        bool prevH2ConsumptionFirstRun = state.firstRunH2Consumption;

        state.h2ConsumptionSegment = EffectUtils::runSegmentDir(
            state,
            state.hydrogenConsumptionSegmentStart,
            state.hydrogenConsumptionSegmentEnd,
            state.h2ConsumptionColor,
            CRGB(state.h2ConsumptionColor.r / 10, state.h2ConsumptionColor.g / 10, state.h2ConsumptionColor.b / 10),
            state.h2ConsumptionDelay,
            state.h2ConsumptionSegment,
            timers.previousMillisH2Consumption,
            state.firstRunH2Consumption,
            state.h2ConsumptionDirForward
        );

        bool h2ConsumptionReachedTerminal = !prevH2ConsumptionFirstRun &&
            (prevH2ConsumptionIndex == EffectUtils::terminalBound(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd));
        if (h2ConsumptionReachedTerminal) {
            state.fabricationOn = state.fabricationEnabled;
        }
    } else {
        state.firstRunH2Consumption = true;
        state.h2ConsumptionSegment = EffectUtils::initialIndex(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
    }

    if (state.fabricationOn && state.fabricationEnabled) {
        if (state.fabricationEffectType == 1) {
            fireEffect(state.leds, state.fabricationSegmentStart, state.fabricationSegmentEnd);
            EffectUtils::advanceIndexDir(
                state.fabricationDelay,
                state.fabricationSegmentStart,
                state.fabricationSegmentEnd,
                state.fabricationDirForward,
                state.fabricationSegment,
                timers.previousMillisFabrication,
                state.firstRunFabrication
            );
        } else if (state.fabricationEffectType == 2) {
            if (state.fadeEffect) {
                state.fadeEffect->update(
                    state.leds,
                    state.fabricationSegmentStart,
                    state.fabricationSegmentEnd,
                    state.fabricationColor,
                    state.firstRunFabrication,
                    state.fabricationDelay
                );
            }
        } else {
            state.fabricationSegment = EffectUtils::runSegmentDir(
                state,
                state.fabricationSegmentStart,
                state.fabricationSegmentEnd,
                state.fabricationColor,
                CRGB(state.fabricationColor.r / 10, state.fabricationColor.g / 10, state.fabricationColor.b / 10),
                state.fabricationDelay,
                state.fabricationSegment,
                timers.previousMillisFabrication,
                state.firstRunFabrication,
                state.fabricationDirForward
            );
        }
    } else {
        state.firstRunFabrication = true;
        state.fabricationSegment = EffectUtils::initialIndex(state.fabricationDirForward, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    }

    // Disable all later chain states in this mode.
    state.electricityProductionOn = false;
    state.electrolyserOn = false;
    state.hydrogenProductionOn = false;
    state.electricityTransportOn = false;
    state.storageTransportOn = false;
    state.storagePowerstationOn = false;
    state.streetLightOn = false;

    updateInformationLEDs(state, timers);
}

void updateHydrogenFromStorageProgram() {
    // After wind-stop this program uses stored hydrogen to drive downstream stages.
    bool draining = runWindStopDrainSequence();

    state.windOn = false;
    state.solarOn = false;
    state.hydrogenTransportDelayActive = false;

    // While draining, keep Hydrogen Transport and Storage In under drain helper control.
    if (!draining) {
        state.hydrogenTransportOn = false;
        state.hydrogenStorageInOn = false;
    }

    if (!state.hydrogenStorageOutOn && !state.h2ConsumptionOn && !state.fabricationOn) {
        state.hydrogenStorageOutOn = state.hydrogenStorageEnabled;
    }

    int prevStorageOutIndex = state.hydrogenStorageSegment2;
    bool prevStorageOutFirstRun = state.firstRunHydrogenStorage2;
    if (state.hydrogenStorageOutOn && state.hydrogenStorageEnabled) {
        state.hydrogenStorageSegment2 = EffectUtils::runSegmentDir(
            state,
            state.hydrogenStorage2SegmentStart,
            state.hydrogenStorage2SegmentEnd,
            state.hydrogenStorage2Color,
            CRGB(state.hydrogenStorage2Color.r / 10, state.hydrogenStorage2Color.g / 10, state.hydrogenStorage2Color.b / 10),
            state.hydrogenStorage2Delay,
            state.hydrogenStorageSegment2,
            timers.previousMillisHydrogenStorage2,
            state.firstRunHydrogenStorage2,
            state.hydrogenStorage2DirForward
        );

        bool storageOutReachedTerminal = !prevStorageOutFirstRun &&
            (prevStorageOutIndex == EffectUtils::terminalBound(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd));
        if (storageOutReachedTerminal) {
            // Keep Storage Out running continuously; reaching terminal only starts downstream segments.
            state.electricityTransportOn = state.electricityTransportEnabled;
            state.storagePowerstationOn = state.storagePowerstationEnabled;
        }
    } else {
        state.firstRunHydrogenStorage2 = true;
        state.hydrogenStorageSegment2 = EffectUtils::initialIndex(state.hydrogenStorage2DirForward, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
    }

    int prevElectricityTransportIndex = state.electricityTransportSegment;
    bool prevElectricityTransportFirstRun = state.firstRunElectricityTransport;
    if (state.electricityTransportOn && state.electricityTransportEnabled) {
        state.electricityTransportSegment = EffectUtils::runSegmentDir(
            state,
            state.electricityTransportSegmentStart,
            state.electricityTransportSegmentEnd,
            state.electricityTransportColor,
            CRGB(state.electricityTransportColor.r / 10, state.electricityTransportColor.g / 10, state.electricityTransportColor.b / 10),
            state.electricityTransportDelay,
            state.electricityTransportSegment,
            timers.previousMillisElectricityTransport,
            state.firstRunElectricityTransport,
            state.electricityTransportDirForward
        );

        bool electricityTransportReachedTerminal = !prevElectricityTransportFirstRun &&
            (prevElectricityTransportIndex == EffectUtils::terminalBound(state.electricityTransportDirForward, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd));
        if (electricityTransportReachedTerminal) {
            state.fabricationOn = state.fabricationEnabled;
        }
    } else {
        state.firstRunElectricityTransport = true;
        state.electricityTransportSegment = EffectUtils::initialIndex(state.electricityTransportDirForward, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
    }

    if (state.storagePowerstationOn && state.storagePowerstationEnabled) {
        state.storagePowerstationSegment = EffectUtils::runSegmentDir(
            state,
            state.storagePowerstationSegmentStart,
            state.storagePowerstationSegmentEnd,
            state.storagePowerstationColor,
            CRGB(state.storagePowerstationColor.r / 10, state.storagePowerstationColor.g / 10, state.storagePowerstationColor.b / 10),
            state.storagePowerstationDelay,
            state.storagePowerstationSegment,
            timers.previousMillisStoragePowerstation,
            state.firstRunStoragePowerstation,
            state.storagePowerstationDirForward
        );
    } else {
        state.firstRunStoragePowerstation = true;
        state.storagePowerstationSegment = EffectUtils::initialIndex(state.storagePowerstationDirForward, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);
    }

    if (state.h2ConsumptionOn && state.h2ConsumptionEnabled) {
        int prevH2ConsumptionIndex = state.h2ConsumptionSegment;
        bool prevH2ConsumptionFirstRun = state.firstRunH2Consumption;

        state.h2ConsumptionSegment = EffectUtils::runSegmentDir(
            state,
            state.hydrogenConsumptionSegmentStart,
            state.hydrogenConsumptionSegmentEnd,
            state.h2ConsumptionColor,
            CRGB(state.h2ConsumptionColor.r / 10, state.h2ConsumptionColor.g / 10, state.h2ConsumptionColor.b / 10),
            state.h2ConsumptionDelay,
            state.h2ConsumptionSegment,
            timers.previousMillisH2Consumption,
            state.firstRunH2Consumption,
            state.h2ConsumptionDirForward
        );

        bool h2ConsumptionReachedTerminal = !prevH2ConsumptionFirstRun &&
            (prevH2ConsumptionIndex == EffectUtils::terminalBound(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd));
        if (h2ConsumptionReachedTerminal) {
            state.fabricationOn = state.fabricationEnabled;
        }
    } else {
        state.firstRunH2Consumption = true;
        state.h2ConsumptionSegment = EffectUtils::initialIndex(state.h2ConsumptionDirForward, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
    }

    if (state.fabricationOn && state.fabricationEnabled) {
        if (state.fabricationEffectType == 1) {
            fireEffect(state.leds, state.fabricationSegmentStart, state.fabricationSegmentEnd);
            EffectUtils::advanceIndexDir(
                state.fabricationDelay,
                state.fabricationSegmentStart,
                state.fabricationSegmentEnd,
                state.fabricationDirForward,
                state.fabricationSegment,
                timers.previousMillisFabrication,
                state.firstRunFabrication
            );
        } else if (state.fabricationEffectType == 2) {
            if (state.fadeEffect) {
                state.fadeEffect->update(
                    state.leds,
                    state.fabricationSegmentStart,
                    state.fabricationSegmentEnd,
                    state.fabricationColor,
                    state.firstRunFabrication,
                    state.fabricationDelay
                );
            }
        } else {
            state.fabricationSegment = EffectUtils::runSegmentDir(
                state,
                state.fabricationSegmentStart,
                state.fabricationSegmentEnd,
                state.fabricationColor,
                CRGB(state.fabricationColor.r / 10, state.fabricationColor.g / 10, state.fabricationColor.b / 10),
                state.fabricationDelay,
                state.fabricationSegment,
                timers.previousMillisFabrication,
                state.firstRunFabrication,
                state.fabricationDirForward
            );
        }
    } else {
        state.firstRunFabrication = true;
        state.fabricationSegment = EffectUtils::initialIndex(state.fabricationDirForward, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    }

    state.hydrogenStorageOn = state.hydrogenStorageInOn || state.hydrogenStorageOutOn;
    state.electricityProductionOn = false;
    state.electrolyserOn = false;
    state.hydrogenProductionOn = false;
    state.storageTransportOn = false;
    state.streetLightOn = false;

    updateInformationLEDs(state, timers);
}

void updateRelays() {
    if (state.relayManualMode) {
        setRelayWind(state.manualWindRelayOn);
        setRelayElectrolyser(state.manualElectrolyserRelayOn);
        state.electrolyserOn = state.manualElectrolyserRelayOn;
        return;
    }

    // Wind relay follows wind segment state.
    setRelayWind(state.windOn);
    // Electrolyser relay turns on as soon as solar starts and turns off when wind stops.
    bool electrolyserRelayOn = state.windOn && state.solarOn;
    state.electrolyserOn = electrolyserRelayOn;
    setRelayElectrolyser(electrolyserRelayOn);
}

// Effect implementations are provided in src/effects/Effects.cpp

void checkButtonState() {
    uint32_t currentMillis = millis();
    // if general timer active handle timeouts
    if (state.generalTimerActive) {
        if (state.restartDelayActive) {
            uint32_t restartDelayMs = static_cast<uint32_t>(state.restartDelaySeconds) * 1000UL;
            if (currentMillis - timers.restartDelayStartTime >= restartDelayMs) {
                // Start next cycle after waiting period.
                resetAllVariables();
                state.autoStartTriggered = true;
                state.buttonDisabled = true;
                state.generalTimerActive = true;
                state.activeProgram = ProgramVariant::HYDROGEN_FROM_RENEWABLES;
                timers.generalTimerStartTime = currentMillis;
                timers.storageProgramStartTime = 0;
                timers.restartDelayStartTime = 0;
                state.restartDelayActive = false;
                state.windOn = true;
                state.solarOn = false;
                digitalWrite(state.buttonLedPin, LOW);
            }
            return;
        }

        uint32_t windStopMs = static_cast<uint32_t>(state.windStopSeconds) * 1000UL;
        uint32_t renewablesElapsed = currentMillis - timers.generalTimerStartTime;
        if (renewablesElapsed >= windStopMs) {
            // Switch only once from renewables flow to storage flow.
            if (state.activeProgram == ProgramVariant::HYDROGEN_FROM_RENEWABLES &&
                (state.windOn || state.solarOn || state.h2ConsumptionOn || state.fabricationOn)) {
                state.windOn = false;
                state.solarOn = false;
                state.h2ConsumptionOn = false; // Fabrication Direct
                state.fabricationOn = false;   // Fabrication Fire

                // Wind-stop requirement: keep Hydrogen Transport + Storage In dim, then drain sequentially.
                state.emptyPipe = true;
                state.pipeEmpty = false;
                state.hydrogenTransportOn = state.hydrogenTransportEnabled;
                state.hydrogenStorageInOn = state.hydrogenStorageEnabled;
                state.hydrogenStorageOutOn = false;
                state.hydrogenStorageOn = state.hydrogenStorageInOn;
                state.activeProgram = ProgramVariant::HYDROGEN_FROM_STORAGE;
                timers.storageProgramStartTime = currentMillis;

                // Clear non-drain segments immediately for status/visual feedback.
                EffectUtils::clearRange(state, state.windSegmentStart, state.windSegmentEnd);
                EffectUtils::clearRange(state, state.solarSegmentStart, state.solarSegmentEnd);
                EffectUtils::clearRange(state, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
                EffectUtils::clearRange(state, state.fabricationSegmentStart, state.fabricationSegmentEnd);
            }
        }

        if (state.activeProgram == ProgramVariant::HYDROGEN_FROM_STORAGE) {
            uint32_t storageRunMs = static_cast<uint32_t>(state.storageRunSeconds) * 1000UL;
            if (currentMillis - timers.storageProgramStartTime >= storageRunMs) {
                if (state.totalLeds > 0) {
                    fill_solid(state.leds, state.totalLeds, CRGB::Black);
                }

                // Turn everything off and enter configurable restart delay.
                setRelayWind(false);
                setRelayElectrolyser(false);
                state.windOn = false;
                state.solarOn = false;
                state.electricityProductionOn = false;
                state.electrolyserOn = false;
                state.hydrogenTransportOn = false;
                state.hydrogenTransportDelayActive = false;
                state.hydrogenProductionOn = false;
                state.hydrogenStorageInOn = false;
                state.hydrogenStorageOutOn = false;
                state.hydrogenStorageOn = false;
                state.h2ConsumptionOn = false;
                state.fabricationOn = false;
                state.electricityTransportOn = false;
                state.storageTransportOn = false;
                state.storagePowerstationOn = false;
                state.streetLightOn = false;
                state.hydrogenStorageFull = false;
                state.emptyPipe = false;
                state.pipeEmpty = false;
                state.activeProgram = ProgramVariant::HYDROGEN_FROM_RENEWABLES;
                state.restartDelayActive = true;
                timers.restartDelayStartTime = currentMillis;
                timers.storageProgramStartTime = 0;
                state.buttonDisabled = true;
                digitalWrite(state.buttonLedPin, LOW);
            }
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
            // Always leave manual relay mode and start from known OFF outputs.
            state.relayManualMode = false;
            state.manualWindRelayOn = false;
            state.manualElectrolyserRelayOn = false;
            setRelayWind(false);
            setRelayElectrolyser(false);
            digitalWrite(state.buttonLedPin, LOW);
            state.activeProgram = ProgramVariant::HYDROGEN_FROM_RENEWABLES;
            state.windOn = true;
            state.solarOn = false;
            state.buttonDisabled = true;
            state.generalTimerActive = true;
            timers.generalTimerStartTime = currentMillis;
            timers.storageProgramStartTime = 0;
            timers.restartDelayStartTime = 0;
            state.restartDelayActive = false;
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
    state.hydrogenStorageInOn = false;
    state.hydrogenStorageOutOn = false;
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
    state.activeProgram = ProgramVariant::HYDROGEN_FROM_RENEWABLES;
    state.autoStartTriggered = false;
    state.relayManualMode = false;
    state.manualWindRelayOn = false;
    state.manualElectrolyserRelayOn = false;

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
    timers.storageProgramStartTime = 0;

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
        timers.restartDelayStartTime = 0;
        state.restartDelayActive = false;
}

void startProgramAuto() {
    if (state.totalLeds > 0) {
        fill_solid(state.leds, state.totalLeds, CRGB::Black);
    }
    // Always leave manual relay mode and start from known OFF outputs.
    state.relayManualMode = false;
    state.manualWindRelayOn = false;
    state.manualElectrolyserRelayOn = false;
    setRelayWind(false);
    setRelayElectrolyser(false);
    state.autoStartTriggered = true;
    state.buttonDisabled = true;
    state.generalTimerActive = true;
    state.activeProgram = ProgramVariant::HYDROGEN_FROM_RENEWABLES;
    timers.generalTimerStartTime = millis();
    timers.storageProgramStartTime = 0;
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