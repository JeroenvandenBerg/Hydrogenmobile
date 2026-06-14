#include "Hardware.h"
#include "Config.h"
#include <Arduino.h>

extern SystemState state;

namespace {
    CLEDController* gLedController = nullptr;
    uint16_t gCurrentLedCount = 0;
    constexpr uint8_t SAFE_BRIGHTNESS = 16;
    constexpr uint16_t SAFE_MAX_MA = 120;

    template <uint8_t PIN>
    CLEDController* addControllerOnPin(SystemState &state) {
        return &FastLED.addLeds<WS2812B, PIN, COLOR_ORDER>(state.leds, state.totalLeds);
    }

    bool isValidLedDataPin(uint8_t pin) {
        switch (pin) {
            case 0: case 2: case 4: case 5:
            case 12: case 13: case 14: case 15:
            case 16: case 17: case 18: case 19:
            case 21: case 22: case 23:
            case 25: case 26: case 27:
            case 32: case 33:
                return true;
            default:
                return false;
        }
    }

    CLEDController* attachLedController(SystemState &state, uint8_t pin) {
        switch (pin) {
            case 0: return addControllerOnPin<0>(state);
            case 2: return addControllerOnPin<2>(state);
            case 4: return addControllerOnPin<4>(state);
            case 5: return addControllerOnPin<5>(state);
            case 12: return addControllerOnPin<12>(state);
            case 13: return addControllerOnPin<13>(state);
            case 14: return addControllerOnPin<14>(state);
            case 15: return addControllerOnPin<15>(state);
            case 16: return addControllerOnPin<16>(state);
            case 17: return addControllerOnPin<17>(state);
            case 18: return addControllerOnPin<18>(state);
            case 19: return addControllerOnPin<19>(state);
            case 21: return addControllerOnPin<21>(state);
            case 22: return addControllerOnPin<22>(state);
            case 23: return addControllerOnPin<23>(state);
            case 25: return addControllerOnPin<25>(state);
            case 26: return addControllerOnPin<26>(state);
            case 27: return addControllerOnPin<27>(state);
            case 32: return addControllerOnPin<32>(state);
            case 33: return addControllerOnPin<33>(state);
            default: return nullptr;
        }
    }

    uint16_t clampLedCount(uint16_t requested) {
        if (requested < 1) return 1;
        if (requested > NUM_LEDS) return NUM_LEDS;
        return requested;
    }
}

void hardwareInit(SystemState &state) {
    state.totalLeds = clampLedCount(state.totalLeds);
    if (!isValidLedDataPin(state.ledDataPin)) {
        state.ledDataPin = DATA_PIN;
    }
    // Attach FastLED to the LED buffer owned by the system state
    gLedController = attachLedController(state, state.ledDataPin);
    if (!gLedController) {
        state.ledDataPin = DATA_PIN;
        gLedController = attachLedController(state, state.ledDataPin);
    }
    Serial.printf("LED data pin active: %u\n", state.ledDataPin);
    // Keep startup current very low for weak USB supplies/new boards.
    FastLED.setBrightness(SAFE_BRIGHTNESS);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, SAFE_MAX_MA);
    gCurrentLedCount = state.totalLeds;
    fill_solid(state.leds, state.totalLeds, CRGB::Black);
    FastLED.show();

    applyPinConfiguration(state);
}

void applyPinConfiguration(SystemState &state) {
    pinMode(state.buttonPin, INPUT_PULLUP);
    pinMode(state.buttonLedPin, OUTPUT);
    pinMode(state.streetLedPin, OUTPUT);
    pinMode(state.windRelayPin, OUTPUT);
    pinMode(state.electrolyserRelayPin, OUTPUT);
    digitalWrite(state.windRelayPin, LOW);
    digitalWrite(state.electrolyserRelayPin, LOW);
    state.windRelayOutputOn = false;
    state.electrolyserRelayOutputOn = false;

    // Initialize info LED pins
    pinMode(state.windInfoLedPin, OUTPUT);
    pinMode(state.electrolyserInfoLedPin, OUTPUT);
    pinMode(state.hydrogenProductionInfoLedPin, OUTPUT);
    pinMode(state.hydrogenStorageInfoLedPin, OUTPUT);
    pinMode(state.hydrogenConsumptionInfoLedPin, OUTPUT);
    pinMode(state.electricityTransportInfoLedPin, OUTPUT);
    pinMode(state.streetInfoLedPin, OUTPUT);

    // Turn off all info LEDs initially
    digitalWrite(state.windInfoLedPin, LOW);
    digitalWrite(state.electrolyserInfoLedPin, LOW);
    digitalWrite(state.hydrogenProductionInfoLedPin, LOW);
    digitalWrite(state.hydrogenStorageInfoLedPin, LOW);
    digitalWrite(state.hydrogenConsumptionInfoLedPin, LOW);
    digitalWrite(state.electricityTransportInfoLedPin, LOW);
    digitalWrite(state.streetInfoLedPin, LOW);
}

void applyLedCount(SystemState &state) {
    if (!gLedController) return;
    uint16_t requested = clampLedCount(state.totalLeds);
    if (requested == gCurrentLedCount) {
        state.totalLeds = requested;
        return;
    }

    // Clear any pixels we previously drove
    gLedController->setLeds(state.leds, gCurrentLedCount);
    fill_solid(state.leds, gCurrentLedCount, CRGB::Black);
    FastLED.show();

    // Apply the new count and clear the active range so the UI sees a blank state
    state.totalLeds = requested;
    gLedController->setLeds(state.leds, state.totalLeds);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, SAFE_MAX_MA);
    gCurrentLedCount = state.totalLeds;
    fill_solid(state.leds, state.totalLeds, CRGB::Black);
    FastLED.show();
}

void setRelayWind(bool on) {
    digitalWrite(state.windRelayPin, on ? HIGH : LOW);
    state.windRelayOutputOn = on;
}
void setRelayElectrolyser(bool on) {
    digitalWrite(state.electrolyserRelayPin, on ? HIGH : LOW);
    state.electrolyserRelayOutputOn = on;
}
bool readButton() { return digitalRead(state.buttonPin) == LOW; } // active low
