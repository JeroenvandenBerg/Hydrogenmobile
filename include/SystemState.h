// SystemState.h
// Centralized runtime state and timers to replace loose globals.

#pragma once

#include <stdint.h>
// bring in FastLED types and configuration macros
#include <FastLED.h>
#include "Config.h"

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
    int electricityTransportSegment = 0;
    int storageTransportSegment = 0;
    int storagePowerstationSegment = 0;

    // First-run flags
    bool firstRunWind = true;
    bool firstRunSolar = true;
    bool firstRunElectricityProduction = true;
    bool firstRunHydrogenProduction = true;
    bool firstRunHydrogenTransport = true;
    bool firstRunHydrogenStorage = true;
    bool firstRunHydrogenStorage2 = true;
    bool firstRunH2Consumption = true;
    bool firstRunElectricityTransport = true;
    bool firstRunStorageTransport = true;
    bool firstRunStoragePowerstation = true;
    // LED framebuffer owned by the runtime state
    CRGB leds[NUM_LEDS];

    // fadeEffect instance pointer (allocated during setup)
    fadeLeds *fadeEffect = nullptr;
};
// End of SystemState.h