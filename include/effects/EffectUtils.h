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

} // namespace EffectUtils
