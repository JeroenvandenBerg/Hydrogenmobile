#include "Hardware.h"
#include "Config.h"
#include <Arduino.h>

void hardwareInit(SystemState &state) {
    // Attach FastLED to the LED buffer owned by the system state
    FastLED.addLeds<WS2812, DATA_PIN, COLOR_ORDER>(state.leds, NUM_LEDS);
    fill_solid(state.leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BUTTON_LED_PIN, OUTPUT);
    pinMode(STREET_LED_PIN, OUTPUT);
    pinMode(WIND_TURBINE_RELAY_PIN, OUTPUT);
    pinMode(ELECTROLYSER_RELAY_PIN, OUTPUT);
}

void setRelayWind(bool on) { digitalWrite(WIND_TURBINE_RELAY_PIN, on ? HIGH : LOW); }
void setRelayElectrolyser(bool on) { digitalWrite(ELECTROLYSER_RELAY_PIN, on ? HIGH : LOW); }
bool readButton() { return digitalRead(BUTTON_PIN) == LOW; } // active low
