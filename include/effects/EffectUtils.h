#pragma once

#include <FastLED.h>
#include "../SystemState.h"
#include "../LEDs.h"
#include "../../lib/runningLed/runningLed.h"

namespace EffectUtils {

// Clear a segment range safely
inline void clearRange(SystemState &state, int start, int end) {
    clearSegment(state, start, end);
}

// Run forward (left-to-right) running LED effect on a segment
inline int runSegmentForward(SystemState &state,
                             int start,
                             int end,
                             const CRGB &headColor,
                             const CRGB &tailColor,
                             uint16_t delayMs,
                             int &segmentIndex,
                             uint32_t &previousMillis,
                             bool &firstRun) {
    return runningLeds(state.leds,
                       start,
                       end,
                       headColor,
                       tailColor,
                       delayMs,
                       segmentIndex,
                       previousMillis,
                       firstRun);
}

// Run reverse (right-to-left) running LED effect on a segment
inline int runSegmentReverse(SystemState &state,
                             int start,
                             int end,
                             const CRGB &headColor,
                             const CRGB &tailColor,
                             uint16_t delayMs,
                             int &segmentIndex,
                             uint32_t &previousMillis,
                             bool &firstRun) {
    return reverseRunningLeds(state.leds,
                              start,
                              end,
                              headColor,
                              tailColor,
                              delayMs,
                              segmentIndex,
                              previousMillis,
                              firstRun);
}

// Run either forward or reverse based on the flag
inline int runSegmentDir(SystemState &state,
                         int start,
                         int end,
                         const CRGB &headColor,
                         const CRGB &tailColor,
                         uint16_t delayMs,
                         int &segmentIndex,
                         uint32_t &previousMillis,
                         bool &firstRun,
                         bool forward) {
    if (forward) {
        return runSegmentForward(state, start, end, headColor, tailColor, delayMs, segmentIndex, previousMillis, firstRun);
    } else {
        return runSegmentReverse(state, start, end, headColor, tailColor, delayMs, segmentIndex, previousMillis, firstRun);
    }
}

// Helper to compute terminal bound based on direction
inline int terminalBound(bool forward, int start, int end) {
    return forward ? end : start;
}

// Helper to compute initial index based on direction
inline int initialIndex(bool forward, int start, int end) {
    return forward ? start : end;
}

// Advance a segment index without drawing, emulating running effect timing and wrap
inline void advanceIndexDir(uint16_t delayMs,
                            int start,
                            int end,
                            bool forward,
                            int &segmentIndex,
                            uint32_t &previousMillis,
                            bool &firstRun) {
    uint32_t now = millis();
    if (now - previousMillis >= delayMs) {
        previousMillis = now;
        if (forward) {
            segmentIndex++;
            if (segmentIndex > end) segmentIndex = start;
        } else {
            segmentIndex--;
            if (segmentIndex < start) segmentIndex = end;
        }
        firstRun = false;
    }
}

// Check if a segment should be active based on its trigger
inline bool isTriggerActive(const SystemState &state, TriggerType trigger) {
    switch (trigger) {
        case TriggerType::ALWAYS_ON:
            return true;
        case TriggerType::WIND:
            return state.windOn;
        case TriggerType::ELECTRICITY_PROD:
            return state.electricityProductionOn;
        case TriggerType::ELECTROLYSER:
            return state.electrolyserOn;
        case TriggerType::HYDROGEN_PROD:
            return state.hydrogenProductionOn;
        case TriggerType::HYDROGEN_TRANSPORT:
            return state.hydrogenTransportOn;
        case TriggerType::HYDROGEN_STORAGE:
            return state.hydrogenStorageOn;
        case TriggerType::H2_CONSUMPTION:
            return state.h2ConsumptionOn;
        case TriggerType::FABRICATION:
            return state.fabricationOn;
        case TriggerType::ELECTRICITY_TRANSPORT:
            return state.electricityTransportOn;
        case TriggerType::STORAGE_TRANSPORT:
            return state.storageTransportOn;
        case TriggerType::STORAGE_POWERSTATION:
            return state.storagePowerstationOn;
        default:
            return false;
    }
}

} // namespace EffectUtils
