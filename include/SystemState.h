// SystemState.h
// Centralized runtime state and timers to replace loose globals.

#pragma once

#include <stdint.h>
// bring in FastLED types and configuration macros
#include <FastLED.h>
#include "Config.h"

// Trigger types for segment activation
enum class TriggerType : uint8_t {
    ALWAYS_ON = 0,           // Segment always active (when enabled)
    WIND = 1,                // Activated by windOn state
    ELECTRICITY_PROD = 2,    // Activated by electricityProductionOn
    ELECTROLYSER = 3,        // Activated by electrolyserOn
    HYDROGEN_PROD = 4,       // Activated by hydrogenProductionOn
    HYDROGEN_TRANSPORT = 5,  // Activated by hydrogenTransportOn
    HYDROGEN_STORAGE = 6,    // Activated by hydrogenStorageOn
    H2_CONSUMPTION = 7,      // Activated by h2ConsumptionOn
    FABRICATION = 8,         // Activated by fabricationOn
    ELECTRICITY_TRANSPORT = 9, // Activated by electricityTransportOn
    STORAGE_TRANSPORT = 10,  // Activated by storageTransportOn
    STORAGE_POWERSTATION = 11 // Activated by storagePowerstationOn
};

struct Timers {
    uint32_t previousButtonCheckMillis = 0;
    uint32_t buttonDisableStartTime = 0;
    uint32_t generalTimerStartTime = 0;
    uint32_t previousMillisWind = 0;
    uint32_t previousMillisSolar = 0;
    uint32_t previousMillisElectricityProduction = 0;
    uint32_t previousMillisElectrolyser = 0;
    uint32_t previousMillisHydrogenTransport = 0;
    uint32_t previousMillisHydrogenProduction = 0;
    uint32_t previousMillisFabrication = 0;
    uint32_t previousMillisHydrogenStorage = 0;
    uint32_t previousMillisHydrogenStorage2 = 0;
    uint32_t previousMillisH2Consumption = 0;
    uint32_t hydrogenStorageFullStartTime = 0;
    uint32_t previousMillisElectricityTransport = 0;
    uint32_t previousMillisStorageTransport = 0;
    uint32_t previousMillisStoragePowerstation = 0;
    uint32_t hydrogenStorageFullTimer = 0;
};

// forward-declare fadeLeds (global scope) so we can keep a pointer here
class fadeLeds;

struct SystemState {
    // Button / timers / flags
    bool buttonDisabled = false;
    bool generalTimerActive = false;
    bool storageTimerStarted = false;

    // Mode flags
    bool windOn = false;
    bool solarOn = false;
    bool electricityProductionOn = false;
    bool electrolyserOn = false;
    bool hydrogenTransportOn = false;
    bool hydrogenTransportDelayActive = false;
    bool hydrogenProductionOn = false;
    bool hydrogenStorageOn = false;
    bool hydrogenStorageFull = false;
    bool h2ConsumptionOn = false;
    bool fabricationOn = false;
    bool electricityTransportOn = false;
    bool storageTransportOn = false;
    bool storagePowerstationOn = false;
    bool streetLightOn = false;
    bool emptyPipe = false;
    bool pipeEmpty = false;

    // Test mode
    bool testMode = false;
    int testSegmentStart = 0;
    int testSegmentEnd = 0;
    int testSegmentIndex = 0;
    bool testDirForward = true;
    int testEffectType = 0; // 0=Running, 1=Fire, 2=Fade
    CRGB testColor = CRGB::White;
    int testDelay = 500;
    int testPhase = 0; // 0=LED check, 1=effect demo
    uint32_t testPhaseStartTime = 0;

    bool autoStartEnabled = false;
    bool autoStartTriggered = false;
    uint16_t hydrogenTransportDelaySeconds = 15; // Delay between electrolyser and hydrogen transport (seconds)

    // Enable flags per segment/effect
    bool windEnabled = true;
    bool solarEnabled = true;
    bool electricityProductionEnabled = true;
    bool electrolyserEnabled = true;
    bool hydrogenProductionEnabled = true;
    bool hydrogenTransportEnabled = true;
    bool hydrogenStorageEnabled = true; // applies to both storage segments
    bool h2ConsumptionEnabled = true;
    bool fabricationEnabled = true;
    bool electricityTransportEnabled = true;
    bool storageTransportEnabled = true;
    bool storagePowerstationEnabled = true;

    // Segment indices
    int windSegment = 0;
    // Allow runtime overrides (persisted by web UI) for all segment start/end
    int windSegmentStart = WIND_LED_START;
    int windSegmentEnd = WIND_LED_END;
    int solarSegmentStart = SOLAR_LED_START;
    int solarSegmentEnd = SOLAR_LED_END;
    int electricityProductionSegmentStart = ELECTRICITY_PRODUCTION_LED_START;
    int electricityProductionSegmentEnd = ELECTRICITY_PRODUCTION_LED_END;
    int hydrogenProductionSegmentStart = HYDROGEN_PRODUCTION_LED_START;
    int hydrogenProductionSegmentEnd = HYDROGEN_PRODUCTION_LED_END;
    int hydrogenTransportSegmentStart = HYDROGEN_TRANSPORT_LED_START;
    int hydrogenTransportSegmentEnd = HYDROGEN_TRANSPORT_LED_END;
    int hydrogenStorage1SegmentStart = HYDROGEN_STORAGE1_LED_START;
    int hydrogenStorage1SegmentEnd = HYDROGEN_STORAGE1_LED_END;
    int hydrogenStorage2SegmentStart = HYDROGEN_STORAGE2_LED_START;
    int hydrogenStorage2SegmentEnd = HYDROGEN_STORAGE2_LED_END;
    int hydrogenConsumptionSegmentStart = HYDROGEN_CONSUMPTION_LED_START;
    int hydrogenConsumptionSegmentEnd = HYDROGEN_CONSUMPTION_LED_END;
    int fabricationSegmentStart = FABRICATION_LED_START;
    int fabricationSegmentEnd = FABRICATION_LED_END;
    int electricityTransportSegmentStart = ELECTRICITY_TRANSPORT_LED_START;
    int electricityTransportSegmentEnd = ELECTRICITY_TRANSPORT_LED_END;
    int storageTransportSegmentStart = STORAGE_TRANSPORT_LED_START;
    int storageTransportSegmentEnd = STORAGE_TRANSPORT_LED_END;
    int storagePowerstationSegmentStart = STORAGE_POWERSTATION_LED_START;
    int storagePowerstationSegmentEnd = STORAGE_POWERSTATION_LED_END;
    int solarSegment = 0;
    int electricityProductionSegment = 0;
    int hydrogenTransportSegment = 0;
    int hydrogenProductionSegment = 0;
    int hydrogenStorageSegment1 = 0;
    int hydrogenStorageSegment2 = 0;
    int h2ConsumptionSegment = 0;
    int fabricationSegment = 0;
    int electricityTransportSegment = 0;
    int storageTransportSegment = 0;
    int storagePowerstationSegment = 0;

    // Direction flags for running effects (true = forward, false = reverse)
    bool windDirForward = true;
    bool solarDirForward = false; // historically ran reverse
    bool electricityProductionDirForward = true;
    bool hydrogenTransportDirForward = true;
    bool hydrogenStorage1DirForward = true; // fill direction; emptying will be opposite
    bool hydrogenStorage2DirForward = true; // fill direction; emptying will be opposite
    bool h2ConsumptionDirForward = true;
    bool electricityTransportDirForward = true;
    bool storageTransportDirForward = true;
    bool storagePowerstationDirForward = true;
    bool hydrogenProductionDirForward = true;
    bool fabricationDirForward = true;

    // Per-segment LED animation delays (milliseconds)
    int windDelay = LED_DELAY;
    int solarDelay = LED_DELAY;
    int electricityProductionDelay = LED_DELAY;
    int hydrogenTransportDelay = LED_DELAY;
    int hydrogenStorage1Delay = LED_DELAY;
    int hydrogenStorage2Delay = LED_DELAY;
    int h2ConsumptionDelay = LED_DELAY;
    int electricityTransportDelay = LED_DELAY;
    int storageTransportDelay = LED_DELAY2;
    int storagePowerstationDelay = LED_DELAY2;
    int hydrogenProductionDelay = LED_DELAY;
    int fabricationDelay = LED_DELAY;

    // Effect type per running segment (0 = running, 1 = fire)
    int windEffectType = 0;
    int solarEffectType = 0;
    int electricityProductionEffectType = 0;
    int hydrogenTransportEffectType = 0;
    int hydrogenStorage1EffectType = 0;
    int hydrogenStorage2EffectType = 0;
    int h2ConsumptionEffectType = 0;
    int electricityTransportEffectType = 0;
    int storageTransportEffectType = 0;
    int storagePowerstationEffectType = 0;

    // Effect types for non-running segments
    // Unified mapping for all segments: 0=Running, 1=Fire, 2=Fade
    int hydrogenProductionEffectType = 0;
    int fabricationEffectType = 0;

    // Activation triggers - configure which state activates each segment
    TriggerType windTrigger = TriggerType::WIND;
    TriggerType solarTrigger = TriggerType::WIND;
    TriggerType electricityProductionTrigger = TriggerType::ELECTRICITY_PROD;
    TriggerType electrolyserTrigger = TriggerType::ELECTROLYSER;  // Set to true when electricity production reaches terminal
    TriggerType hydrogenProductionTrigger = TriggerType::ELECTROLYSER;
    TriggerType hydrogenTransportTrigger = TriggerType::HYDROGEN_TRANSPORT;
    TriggerType hydrogenStorage1Trigger = TriggerType::HYDROGEN_STORAGE;
    TriggerType hydrogenStorage2Trigger = TriggerType::HYDROGEN_STORAGE;
    TriggerType h2ConsumptionTrigger = TriggerType::H2_CONSUMPTION;
    TriggerType fabricationTrigger = TriggerType::FABRICATION;
    TriggerType electricityTransportTrigger = TriggerType::ELECTRICITY_TRANSPORT;
    TriggerType storageTransportTrigger = TriggerType::STORAGE_TRANSPORT;
    TriggerType storagePowerstationTrigger = TriggerType::STORAGE_POWERSTATION;

    // First-run flags
    bool firstRunWind = true;
    bool firstRunSolar = true;
    bool firstRunElectricityProduction = true;
    bool firstRunHydrogenProduction = true;
    bool firstRunHydrogenTransport = true;
    bool firstRunHydrogenStorage = true;
    bool firstRunHydrogenStorage2 = true;
    bool firstRunH2Consumption = true;
    bool firstRunFabrication = true;
    bool firstRunElectricityTransport = true;
    bool firstRunStorageTransport = true;
    bool firstRunStoragePowerstation = true;
    // LED framebuffer owned by the runtime state
    CRGB leds[NUM_LEDS];

    // fadeEffect instance pointer (allocated during setup)
    fadeLeds *fadeEffect = nullptr;

    // Per-segment colors (used for Running and Fade)
    CRGB windColor = WIND_COLOR_ACTIVE;
    CRGB solarColor = WIND_COLOR_ACTIVE; // using same default as wind unless configured separately
    CRGB electricityProductionColor = WIND_COLOR_ACTIVE;
    CRGB hydrogenProductionColor = HYDROGEN_PRODUCTION_COLOR_ACTIVE;
    CRGB hydrogenTransportColor = HYDROGEN_PRODUCTION_COLOR_ACTIVE;
    CRGB hydrogenStorage1Color = HYDROGEN_STORAGE_COLOR_ACTIVE;
    CRGB hydrogenStorage2Color = HYDROGEN_STORAGE_COLOR_ACTIVE;
    CRGB h2ConsumptionColor = HYDROGEN_CONSUMPTION_COLOR_ACTIVE;
    CRGB fabricationColor = HYDROGEN_CONSUMPTION_COLOR_ACTIVE;
    CRGB electricityTransportColor = ELECTRICITY_TRANSPORT_COLOR_ACTIVE;
    CRGB storageTransportColor = HYDROGEN_CONSUMPTION_COLOR_ACTIVE;
    CRGB storagePowerstationColor = HYDROGEN_CONSUMPTION_COLOR_ACTIVE;

    // Editable segment names (persisted via Web UI)
    String windName = "Wind";
    String solarName = "Solar";
    String electricityProductionName = "Electricity Production";
    String hydrogenProductionName = "Hydrogen Production";
    String hydrogenTransportName = "Hydrogen Transport";
    String hydrogenStorage1Name = "Hydrogen Storage 1";
    String hydrogenStorage2Name = "Hydrogen Storage 2";
    String h2ConsumptionName = "Hydrogen Consumption";
    String fabricationName = "Fabrication";
    String electricityTransportName = "Electricity Transport";
    String storageTransportName = "Storage Transport";
    String storagePowerstationName = "Storage Powerstation";

    // ---------------- Custom segments ----------------
    static constexpr int MAX_CUSTOM_SEGMENTS = 3;
    struct CustomSegment {
        bool inUse = false;
        String name = "Custom";
        int start = 0;
        int end = 0;
        bool dirForward = true;
        bool enabled = true;
        int delay = LED_DELAY;
        int effectType = 0; // 0=Running, 1=Fire, 2=Fade
        CRGB color = CRGB::White;
        TriggerType trigger = TriggerType::ALWAYS_ON;
        // runtime fields
        int segmentIndex = 0;
        bool firstRun = true;
        uint32_t prevMillis = 0; // for timing per custom segment
    };
    CustomSegment custom[MAX_CUSTOM_SEGMENTS];
};
// End of SystemState.h