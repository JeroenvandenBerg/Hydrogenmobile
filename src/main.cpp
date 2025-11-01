#include <Arduino.h>
#include "FastLED.h"
#include "runningLed.h"
#include "fireEffect.h"
#include "fadeLeds.h"

// ========================== Constants and Macros ==========================
// Define the number of LEDs, data pin, and color order for the LED strip
#define NUM_LEDS 600
#define DATA_PIN 4
#define COLOR_ORDER GRB

// Define LED segment ranges for different effects
#define WIND_LED_START 10
#define WIND_LED_END 45
#define SOLAR_LED_START 46 // Solar segment start
#define SOLAR_LED_END 74   // Solar segment end
#define ELECTRICITY_PRODUCTION_LED_START 75 // Electricity production segment start
#define ELECTRICITY_PRODUCTION_LED_END 89   // Electricity production segment end

#define HYDROGEN_PRODUCTION_LED_START 90 //electrolyser +13
#define HYDROGEN_PRODUCTION_LED_END 101 //electrolyser
#define HYDROGEN_TRANSPORT_LED_START 102 //electrolyser
#define HYDROGEN_TRANSPORT_LED_MID 109 //electrolyser
#define HYDROGEN_TRANSPORT_LED_END 191 //electrolyser
#define HYDROGEN_STORAGE1_LED_START 192
#define HYDROGEN_STORAGE1_LED_END 197
#define HYDROGEN_STORAGE2_LED_START 198
#define HYDROGEN_STORAGE2_LED_END 203

#define HYDROGEN_CONSUMPTION_LED_START 475 //LEDS to fabrication
#define HYDROGEN_CONSUMPTION_LED_END 548 //LEDS to fabrication
#define FABRICATION_LED_START 0 //fire 536
#define FABRICATION_LED_END 9 //fire 546
#define ELECTRICITY_TRANSPORT_LED_START 425 //electrolyser
#define ELECTRICITY_TRANSPORT_LED_END 474 //electrolyser
#define STORAGE_TRANSPORT_LED_START 204 // Storage transport segment start
#define STORAGE_TRANSPORT_LED_END 392   // Storage transport segment end
#define STORAGE_POWERSTATION_LED_START 393 // Storage powerstation segment start
#define STORAGE_POWERSTATION_LED_END 424   // Storage powerstation segment end

// Define informational LED positions
#define WIND_INFO_LED 62
#define ELECTROLYSER_INFO_LED 63
#define HYDROGEN_PRODUCTION_INFO_LED 64
#define HYDROGEN_STORAGE_INFO_LED 65
#define HYDROGEN_CONSUMPTION_INFO_LED 66
//#define FABRICATION_INFO_LED 55
#define ELECTRICITY_TRANSPORT_INFO_LED 67
#define STREET_LED 68 // Pin for the new LED

// Define colors for active states
#define WIND_COLOR_ACTIVE CRGB(255, 255, 0) // Yellow
#define HYDROGEN_PRODUCTION_COLOR_ACTIVE CRGB(0, 255, 0) // Green
#define HYDROGEN_STORAGE_COLOR_ACTIVE CRGB(0, 255, 0) // Green
#define HYDROGEN_CONSUMPTION_COLOR_ACTIVE CRGB(0, 255, 0) // Green
#define ELECTRICITY_TRANSPORT_COLOR_ACTIVE CRGB(255, 255, 0) // Yellow
#define LED_DELAY 200 // Delay for LED effects
#define LED_DELAY2 100 // Delay for LED effects

// Define button settings
#define BUTTON_PIN 0 // Pin for the button
#define BUTTON_CHECK_INTERVAL 250 // Interval to check button state (ms)
#define BUTTON_LED_PIN 2 // Pin for the button LED
// Global variable to track button disabled state
bool buttonDisabled = false;
uint32_t buttonDisableStartTime = 0;

//Global timer variables
uint32_t generalTimerStartTime = 0; // Tracks the start time of the general timer
bool generalTimerActive = false;   // Tracks whether the general timer is active
bool storageTimerStarted = false; // Tracks whether the storage timer has started
uint32_t windTime = 42000;
uint32_t runTime = 90000;
uint32_t hydrogenProductionDelay = 3000; // Delay before starting hydrogen production
u_int32_t hydrogenStorageDelay = 3000; // Delay before starting hydrogen storage

//define Streetlight settings
#define STREET_LED_PIN 14 // Pin for the new LED

//define relays
#define WIND_TURBINE_RELAY_PIN 12 // Relay for wind turbines
#define ELECTROLYSER_RELAY_PIN 13 // Relay for electrolyser

// ========================== Global Variables ==========================
// Array to store LED states
CRGB leds[NUM_LEDS];

// Fade effect object with a 2000ms duration
fadeLeds fadeEffect(2000);

// State variables to track the status of different processes
bool windOn = false;
bool solarOn = false; // State variable for solar segment
bool electricityProductionOn = false; // State variable for electricity production segment
bool electrolyserOn = false;
bool hydrogenTransportOn = false;
bool hydrogenProductionOn = false;
bool hydrogenStorageOn = false;
bool hydrogenStorageFull = false;
bool h2ConsumptionOn = false;
bool fabricationOn = false;
bool electricityTransportOn = false;
bool storageTransportOn = false; // State variable for storage transport segment
bool storagePowerstationOn = false; // State variable for storage powerstation segment
bool streetLightOn = false;
bool emptyPipe = false; 
bool pipeEmpty = false;

// Timers for managing delays and animations
uint32_t previousButtonCheckMillis = 0;
uint32_t previousMillisWind = 0;
uint32_t previousMillisSolar = 0; // Timer for solar animation
uint32_t previousMillisElectricityProduction = 0; // Timer for electricity production animation
uint32_t previousMillisElectrolyser = 0;
uint32_t previousMillisHydrogenTransport = 0;
uint32_t previousMillisHydrogenProduction = 0;
uint32_t previousMillisHydrogenStorage = 0;
uint32_t previousMillisHydrogenStorage2 = 0;
uint32_t previousMillisH2Consumption = 0;
uint32_t hydrogenStorageFullStartTime = 0;
uint32_t previousMillisElectricityTransport = 0;
uint32_t previousMillisStorageTransport = 0; // Timer for storage transport animation
uint32_t previousMillisStoragePowerstation = 0; // Timer for storage powerstation animation
uint32_t hydrogenStorageFullTimer = 0; // Timer for hydrogen storage full state


// Segment indices for animations
int windSegment = WIND_LED_START;
int solarSegment = SOLAR_LED_END; // Segment index for solar animation
int electricityProductionSegment = ELECTRICITY_PRODUCTION_LED_START; // Segment index for electricity production animation
int hydrogenTransportSegment = HYDROGEN_TRANSPORT_LED_START;
int hydrogenProductionSegment = HYDROGEN_PRODUCTION_LED_START;
int hydrogenStorageSegment1 = HYDROGEN_STORAGE1_LED_START;
int hydrogenStorageSegment2 = HYDROGEN_STORAGE2_LED_START;
int h2ConsumptionSegment = HYDROGEN_CONSUMPTION_LED_START;
int electricityTransportSegment = ELECTRICITY_TRANSPORT_LED_START;
int storageTransportSegment = STORAGE_TRANSPORT_LED_START; // Segment index for storage transport animation END because of connected other way around
int storagePowerstationSegment = STORAGE_POWERSTATION_LED_START; // Segment index for storage powerstation animation


// Flags to track the first run of animations
bool firstRunWind = true;
bool firstRunSolar = true; // First run flag for solar animation
bool firstRunElectricityProduction = true; // First run flag for electricity production animation
bool firstRunHydrogenProduction = true;
bool firstRunHydrogenTransport = true;
bool firstRunHydrogenStorage = true;
bool firstRunHydrogenStorage2 = true;
bool firstRunH2Consumption = true;
bool firstRunElectricityTransport = true;
bool firstRunStorageTransport = true; // First run flag for storage transport animation
bool firstRunStoragePowerstation = true; // First run flag for storage powerstation animation


// ========================== Function Declarations ==========================
// Function prototypes for initialization, updates, and effects
void initializeHardware();
void updateSegments();
void updateWindEffect();
void updateElectricityProductionEffect(); // Update the electricity production effect (New Segment)
void updateElectrolyserEffect();
void updateHydrogenTransportEffect();
void updateHydrogenProductionEffect();
void updateHydrogenStorageEffect();
void updateH2ConsumptionEffect();
void updateFabricationEffect();
void updateElectricityEffect();
void updateStorageTransportEffect();
void updateRelays();
void updateInformationLEDs();
void checkButtonState();
void resetAllVariables(); // Reset all variables at startup

// ========================== Setup and Loop ==========================

// Arduino setup function, called once at startup
void setup() {
    initializeHardware(); // Initialize hardware components
    digitalWrite(BUTTON_LED_PIN, HIGH); // Turn on the button LED
    resetAllVariables(); // Reset all variables at startup
    windOn = true; // Start with wind effect enabled with button
}
// Arduino loop function, called repeatedly
void loop() {
    

    //checkButtonState(); // Check the button state and toggle wind effect
    updateSegments(); // Update all LED segments based on the current state
    updateRelays(); // Update the relays
    //updateInformationLEDs(); // Update informational LEDs
    
    FastLED.show(); // Refresh the LED strip to display changes
}

// ========================== Function Definitions ==========================

// Initialize hardware components
void initializeHardware() {
    // Configure the LED strip
    FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
    Serial.begin(115200); // Start serial communication for debugging
    fill_solid(leds, NUM_LEDS, CRGB::Black); // Turn off all LEDs initially
    FastLED.show(); // Apply the changes to the LED strip
    pinMode(BUTTON_PIN, INPUT_PULLUP); // Configure the button pin as input with pull-up resistor
    pinMode(BUTTON_LED_PIN, OUTPUT); // Configure the button LED pin as output
    pinMode(STREET_LED_PIN, OUTPUT); // Configure the street LED pin as output
    pinMode(WIND_TURBINE_RELAY_PIN, OUTPUT); // Configure wind turbine relay pin as output
    pinMode(ELECTROLYSER_RELAY_PIN, OUTPUT); // Configure electrolyser relay pin as output
}


// Update all LED segments
void updateSegments() {
    
    updateWindEffect(); // Update the wind effect (Segment 1)
    updateElectricityProductionEffect(); // Update the electricity production effect (New Segment)
    updateElectrolyserEffect(); // Update the electrolyser effect
    updateHydrogenProductionEffect(); // Update hydrogen production effect (Segment 2)
    updateHydrogenTransportEffect(); // Update hydrogen transport effect (Segment 3)
   
    updateHydrogenStorageEffect(); // Update hydrogen storage effect (Segment 3)
    updateH2ConsumptionEffect(); // Update H2 consumption effect (Segment 5)
    updateFabricationEffect(); // Update fabrication effect (Segment 4)
    updateElectricityEffect(); // Update electricity transport effect
    updateStorageTransportEffect(); // Update storage transport effect (Segment 6)
}

void updateRelays() {
    // Control the wind turbine relay
    if (windOn) {
        digitalWrite(WIND_TURBINE_RELAY_PIN, HIGH); // Turn on wind turbine relay
    } else {
        digitalWrite(WIND_TURBINE_RELAY_PIN, LOW); // Turn off wind turbine relay
    }

    // Control the electrolyser relay
    if (electrolyserOn) {
        digitalWrite(ELECTROLYSER_RELAY_PIN, HIGH); // Turn on electrolyser relay
    } else {
        digitalWrite(ELECTROLYSER_RELAY_PIN, LOW); // Turn off electrolyser relay
    }
}

// Handle Wind Effect (Segment 1)
void updateWindEffect() {
    if (windOn) {
        // Animate the wind segment using running LEDs
        windSegment = runningLeds(
            leds,
            WIND_LED_START,
            WIND_LED_END,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            windSegment,
            previousMillisWind,
            firstRunWind
        );
        solarSegment = reverseRunningLeds(
            leds,
            SOLAR_LED_START,
            SOLAR_LED_END,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            solarSegment,
            previousMillisSolar,
            firstRunSolar
        );

        // If the wind animation reaches the end, enable the electrolyser
        if (windSegment == WIND_LED_END || solarSegment == SOLAR_LED_START) {
                electricityProductionOn = true; // Start electricity production        
        }

    } else {
        // Turn off the wind segment and reset variables
        fill_solid(leds + WIND_LED_START, WIND_LED_END - WIND_LED_START + 1, CRGB::Black);
        firstRunWind = true;
        windSegment = WIND_LED_START;
        //electrolyserOn = false;
        electricityProductionOn = false; // Stop electricity production
        fill_solid(leds + SOLAR_LED_START, SOLAR_LED_END - SOLAR_LED_START + 1, CRGB::Black);
        firstRunSolar = true;
        solarSegment = SOLAR_LED_END;
        electricityProductionOn = false;
    }   
}
void updateElectricityProductionEffect() {
    if (electricityProductionOn) {
        // Animate the electricity production segment using running LEDs
        electricityProductionSegment = runningLeds(
            leds,
            ELECTRICITY_PRODUCTION_LED_START,
            ELECTRICITY_PRODUCTION_LED_END,
            WIND_COLOR_ACTIVE,
            CRGB(WIND_COLOR_ACTIVE.r / 10, WIND_COLOR_ACTIVE.g / 10, WIND_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            electricityProductionSegment,
            previousMillisElectricityProduction,
            firstRunElectricityProduction
        );

        // If the electricity production animation reaches the end, stop the effect
        if (electricityProductionSegment == ELECTRICITY_PRODUCTION_LED_END) {
            if (!electrolyserOn) { // Only set the timer once
            electrolyserOn = true;
            previousMillisElectrolyser = millis(); // Start the electrolyser timer
            }
        }
    } else {
        // Turn off the electricity production segment and reset variables
        fill_solid(leds + ELECTRICITY_PRODUCTION_LED_START, ELECTRICITY_PRODUCTION_LED_END - ELECTRICITY_PRODUCTION_LED_START + 1, CRGB::Black);
        firstRunElectricityProduction = true;
        electricityProductionSegment = ELECTRICITY_PRODUCTION_LED_START;
        electrolyserOn = false;
    }
}


// Handle Electrolyser Effect
void updateElectrolyserEffect() {
    if (electrolyserOn) {
        // Enable hydrogen production after a delay
        if (millis() - previousMillisElectrolyser >= hydrogenProductionDelay) {
            hydrogenProductionOn = true; // Start hydrogen transport
        }
    } else {
        hydrogenProductionOn = false;
    }
}



// Handle Hydrogen Production Effect (Segment 2)
void updateHydrogenProductionEffect() {
    if (hydrogenProductionOn) {
        // Apply a fade effect to the hydrogen production segment
        fadeEffect.update(leds, HYDROGEN_PRODUCTION_LED_START, HYDROGEN_PRODUCTION_LED_END, HYDROGEN_PRODUCTION_COLOR_ACTIVE, firstRunHydrogenProduction);
        hydrogenTransportOn = true; // Enable hydrogen storage
    } else {
        // Turn off the hydrogen production segment and reset variables
        fill_solid(leds + HYDROGEN_PRODUCTION_LED_START, HYDROGEN_PRODUCTION_LED_END - HYDROGEN_PRODUCTION_LED_START + 1, CRGB::Black);
        firstRunHydrogenProduction = true;
        hydrogenTransportOn = false;
    }
}


// Handle Hydrogen Transport Effect (Segment 3)
void updateHydrogenTransportEffect() {
    if (hydrogenTransportOn) {
        // Animate the hydrogen transport segment using running LEDs
        hydrogenTransportSegment = runningLeds(
            leds,
            HYDROGEN_TRANSPORT_LED_START,
            HYDROGEN_TRANSPORT_LED_END,
            HYDROGEN_PRODUCTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_PRODUCTION_COLOR_ACTIVE.r / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.g / 10, HYDROGEN_PRODUCTION_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            hydrogenTransportSegment,
            previousMillisHydrogenTransport,
            firstRunHydrogenTransport
        );

        
        // If the transport animation reaches the end, enable hydrogen production
        
        if (hydrogenTransportSegment == HYDROGEN_TRANSPORT_LED_MID) {
            h2ConsumptionOn = true; // Start H2 consumption
            
        }
        if (hydrogenTransportSegment == HYDROGEN_TRANSPORT_LED_END) {
            hydrogenStorageOn = true; // Start hydrogen production
            emptyPipe = true;
            
        }
        
    } 
    else if(hydrogenStorageFull){
        
        if (hydrogenTransportSegment ==HYDROGEN_TRANSPORT_LED_START){
            pipeEmpty = true;   
        }

        if (emptyPipe){
            fill_solid(leds + HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END - HYDROGEN_TRANSPORT_LED_START + 1, CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 20, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 20, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 20));
            hydrogenTransportSegment = HYDROGEN_TRANSPORT_LED_START;
            emptyPipe = false;
            
        }
                
        if (!pipeEmpty){
            hydrogenTransportSegment = runningLeds(
                leds,
                HYDROGEN_TRANSPORT_LED_START,
                HYDROGEN_TRANSPORT_LED_END,
                HYDROGEN_PRODUCTION_COLOR_ACTIVE,
                CRGB::Black,
                LED_DELAY, // Animation speed
                hydrogenTransportSegment,
                previousMillisHydrogenTransport,
                firstRunHydrogenTransport
            );
        }else if (pipeEmpty){
                // Ensure the entire segment is black when the animation ends
                fill_solid(leds + HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END - HYDROGEN_TRANSPORT_LED_START + 1, CRGB::Black);
        
            }
        
        hydrogenStorageOn = false; // Reset hydrogen storage state
    }
    else {
        // Turn off the hydrogen transport segment and reset variables
        //fill_solid(leds + HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END - HYDROGEN_TRANSPORT_LED_START + 1, CRGB::Black);
        firstRunHydrogenTransport = true;
        hydrogenTransportSegment = HYDROGEN_TRANSPORT_LED_START;
        hydrogenStorageOn = false; // Reset hydrogen storage state
        bool emptyPipe = false; 
        bool pipeEmpty = false;

        //electricityTransportOn = false; // Reset electricity transport state
    }
}

// Handle Hydrogen Storage Effect (Segment 3)
void updateHydrogenStorageEffect() {
    if (hydrogenStorageOn) {
        // Animate the hydrogen storage segment using running LEDs
        hydrogenStorageSegment1 = runningLeds(
            leds,
            HYDROGEN_STORAGE1_LED_START,
            HYDROGEN_STORAGE1_LED_END,
            HYDROGEN_STORAGE_COLOR_ACTIVE,
            CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            hydrogenStorageSegment1,
            previousMillisHydrogenStorage,
            firstRunHydrogenStorage
        );
        hydrogenStorageSegment2 = runningLeds(
            leds,
            HYDROGEN_STORAGE2_LED_START,
            HYDROGEN_STORAGE2_LED_END,
            HYDROGEN_STORAGE_COLOR_ACTIVE,
            CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            hydrogenStorageSegment2,
            previousMillisHydrogenStorage2,
            firstRunHydrogenStorage2
        );

        // If the storage animation reaches the end, enable H2 consumption
        if (hydrogenStorageSegment1 == HYDROGEN_STORAGE1_LED_END) {
            
            hydrogenStorageFull = true;
        }

    } else if (hydrogenStorageFull) {
        // Run the reversed LED animation when storage is full but not active
        
        if (!storageTimerStarted) {

            h2ConsumptionOn = false; // Stop H2 consumption
            //fill_solid(leds + HYDROGEN_CONSUMPTION_LED_START, HYDROGEN_CONSUMPTION_LED_END - HYDROGEN_CONSUMPTION_LED_START + 1, CRGB::Green);
            //fill_solid(leds + HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END - HYDROGEN_TRANSPORT_LED_START + 1, CRGB::Black);
            fill_solid(leds + HYDROGEN_STORAGE1_LED_START, HYDROGEN_STORAGE1_LED_END - HYDROGEN_STORAGE1_LED_START + 1, CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10));
            fill_solid(leds + HYDROGEN_STORAGE2_LED_START, HYDROGEN_STORAGE2_LED_END - HYDROGEN_STORAGE2_LED_START + 1, CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10));
            h2ConsumptionSegment = HYDROGEN_CONSUMPTION_LED_START; 
            hydrogenStorageSegment1 = HYDROGEN_STORAGE1_LED_END; // Start from the end
            hydrogenStorageSegment2 = HYDROGEN_STORAGE2_LED_END; // Start from the end
            hydrogenStorageFullTimer = millis(); // Start the timer
            storageTimerStarted = true; // Mark the timer as started
            
        }
        if (millis() - hydrogenStorageFullTimer >= hydrogenStorageDelay) {
            hydrogenStorageSegment1 = reverseRunningLeds(
                leds,
                HYDROGEN_STORAGE1_LED_START, // Start from the end
                HYDROGEN_STORAGE1_LED_END, // Run towards the start
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                LED_DELAY, // Animation speed
                hydrogenStorageSegment1,
                previousMillisHydrogenStorage,
                firstRunHydrogenStorage
            );
            hydrogenStorageSegment2 = reverseRunningLeds(
                leds,
                HYDROGEN_STORAGE2_LED_START, // Start from the end
                HYDROGEN_STORAGE2_LED_END, // Run towards the start
                HYDROGEN_STORAGE_COLOR_ACTIVE,
                CRGB(HYDROGEN_STORAGE_COLOR_ACTIVE.r / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.g / 10, HYDROGEN_STORAGE_COLOR_ACTIVE.b / 10),
                LED_DELAY, // Animation speed
                hydrogenStorageSegment2,
                previousMillisHydrogenStorage2,
                firstRunHydrogenStorage2
            );
        }
        // If the reverse animation reaches the start, mark storage as empty
        if (hydrogenStorageSegment1 == HYDROGEN_STORAGE1_LED_START || hydrogenStorageSegment2 == HYDROGEN_STORAGE2_LED_START) {
            storageTransportOn = true; // Start storage transport
            
        }
    } else {
        // Turn off the hydrogen storage segment and reset variables
        fill_solid(leds + HYDROGEN_STORAGE1_LED_START, HYDROGEN_STORAGE1_LED_END - HYDROGEN_STORAGE1_LED_START + 1, CRGB::Black);
        fill_solid(leds + HYDROGEN_STORAGE2_LED_START, HYDROGEN_STORAGE2_LED_END - HYDROGEN_STORAGE2_LED_START + 1, CRGB::Black);
        firstRunHydrogenStorage = true;
        firstRunHydrogenStorage2 = true;
        hydrogenStorageSegment1 = HYDROGEN_STORAGE1_LED_START;
        hydrogenStorageSegment2 = HYDROGEN_STORAGE2_LED_START;
        //h2ConsumptionOn = false;
        storageTransportOn = false; // Reset storage transport state
        storageTimerStarted = false; // Reset the timer flag
    }
}

// Handle H2 Consumption Effect (Segment 5)
void updateH2ConsumptionEffect() {
    if (h2ConsumptionOn) {
        // Animate the H2 consumption segment using running LEDs
        h2ConsumptionSegment = runningLeds(
            leds,
            HYDROGEN_CONSUMPTION_LED_START,
            HYDROGEN_CONSUMPTION_LED_END,
            HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            h2ConsumptionSegment,
            previousMillisH2Consumption,
            firstRunH2Consumption
        );

        // If the consumption animation reaches the end, enable fabrication
        if (h2ConsumptionSegment == HYDROGEN_CONSUMPTION_LED_END) {
            fabricationOn = true; // Start fabrication
            //electricityTransportOn = true; // Start electricity transport
        }
    } 
    else if(storageTransportOn){
        if (storageTransportSegment == STORAGE_TRANSPORT_LED_END) {
            fabricationOn = true; // Start electricity transport
            
        }
    }
    else {
    // Turn off the H2 consumption segment and reset variables
    fill_solid(leds +HYDROGEN_CONSUMPTION_LED_START, HYDROGEN_CONSUMPTION_LED_END - HYDROGEN_CONSUMPTION_LED_START + 1, CRGB::Black);
    firstRunH2Consumption = true;
    h2ConsumptionSegment = HYDROGEN_CONSUMPTION_LED_START;
    fabricationOn = false;
    }
}

// Handle Fabrication Effect (Segment 4)
void updateFabricationEffect() {
    if (fabricationOn) {
        // Apply a fire effect to the fabrication segment
        fireEffect(leds, FABRICATION_LED_START, FABRICATION_LED_END);
    } else {
        // Turn off the fabrication segment
        fill_solid(leds + FABRICATION_LED_START, FABRICATION_LED_END - FABRICATION_LED_START + 1, CRGB::Black);
        
    }
}

void updateStorageTransportEffect() {
    if (storageTransportOn) {
        // Animate the storage transport segment using running LEDs
        storageTransportSegment = runningLeds(
            leds,
            STORAGE_TRANSPORT_LED_START,
            STORAGE_TRANSPORT_LED_END,
            HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
            CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
            LED_DELAY2, // Animation speed
            storageTransportSegment,
            previousMillisStorageTransport,
            firstRunStorageTransport
        );
        if (storageTransportSegment == STORAGE_TRANSPORT_LED_END) {
            //fabricationOn = true; // Start electricity transport
            storagePowerstationOn = true; // Start electricity transport    
        }
        if (storagePowerstationOn){
            storagePowerstationSegment = runningLeds(
                leds,
                STORAGE_POWERSTATION_LED_START,
                STORAGE_POWERSTATION_LED_END,
                HYDROGEN_CONSUMPTION_COLOR_ACTIVE,
                CRGB(HYDROGEN_CONSUMPTION_COLOR_ACTIVE.r / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.g / 10, HYDROGEN_CONSUMPTION_COLOR_ACTIVE.b / 10),
                LED_DELAY2, // Animation speed
                storagePowerstationSegment,
                previousMillisStoragePowerstation,
                firstRunStoragePowerstation
            );
        }
        
        // If the storage transport animation reaches the end, enable storage powerstation
        if (storagePowerstationSegment == STORAGE_POWERSTATION_LED_END) {
            electricityTransportOn = true; // Start electricity transport
            Serial.println("Electricity transport enabled");
        }
        
    } else {
        // Turn off the storage transport segment and reset variables
        fill_solid(leds + STORAGE_TRANSPORT_LED_START, STORAGE_TRANSPORT_LED_END - STORAGE_TRANSPORT_LED_START + 1, CRGB::Black);
        firstRunStorageTransport = true;
        storageTransportSegment = STORAGE_TRANSPORT_LED_START;
        fill_solid(leds + STORAGE_POWERSTATION_LED_START, STORAGE_POWERSTATION_LED_END - STORAGE_POWERSTATION_LED_START + 1, CRGB::Black);
        firstRunStoragePowerstation = true;
        storagePowerstationSegment = STORAGE_POWERSTATION_LED_START;
        storagePowerstationOn = false; // Reset storage powerstation state
    }
}


// Handle Electricity Transport Effect
void updateElectricityEffect() {
    //if (electricityTransportOn || hydrogenStorageFull) {
    if (electricityTransportOn) {
        // Animate the electricity transport segment using running LEDs
        electricityTransportSegment = runningLeds(
            leds,
            ELECTRICITY_TRANSPORT_LED_START,
            ELECTRICITY_TRANSPORT_LED_END,
            ELECTRICITY_TRANSPORT_COLOR_ACTIVE, // Use a color for electricity transport
            CRGB(ELECTRICITY_TRANSPORT_COLOR_ACTIVE.r / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.g / 10, ELECTRICITY_TRANSPORT_COLOR_ACTIVE.b / 10),
            LED_DELAY, // Animation speed
            electricityTransportSegment,
            previousMillisElectricityTransport,
            firstRunElectricityTransport
        );

        // If the electricity transport animation reaches the end, stop the effect
        if (electricityTransportSegment == ELECTRICITY_TRANSPORT_LED_END) {
            digitalWrite(STREET_LED_PIN, HIGH); // Turn on Street LED
            streetLightOn = true; // Set the street light state
            
        }
    } else {
        // Turn off the electricity transport segment and reset variables
        fill_solid(leds + ELECTRICITY_TRANSPORT_LED_START, ELECTRICITY_TRANSPORT_LED_END - ELECTRICITY_TRANSPORT_LED_START + 1, CRGB::Black);
        firstRunElectricityTransport = true;
        electricityTransportSegment = ELECTRICITY_TRANSPORT_LED_START;
        digitalWrite(STREET_LED_PIN, LOW); // Turn off Street LED
        streetLightOn = false; // Reset the street light state
    }
}

// Update informational LEDs
void updateInformationLEDs() {
    // Set the state of informational LEDs based on the current process states
    leds[WIND_INFO_LED] = windOn ? CRGB::Red : CRGB::Black;
    leds[HYDROGEN_PRODUCTION_INFO_LED] = hydrogenProductionOn ? CRGB::Red : CRGB::Black;
    leds[ELECTROLYSER_INFO_LED] = electrolyserOn ? CRGB::Red : CRGB::Black;
    leds[HYDROGEN_STORAGE_INFO_LED] = hydrogenStorageOn ? CRGB::Red : CRGB::Black;
    leds[HYDROGEN_CONSUMPTION_INFO_LED] = h2ConsumptionOn ? CRGB::Red : CRGB::Black;
    //leds[FABRICATION_INFO_LED] = fabricationOn ? CRGB::Red : CRGB::Black;
    leds[ELECTRICITY_TRANSPORT_INFO_LED] = electricityTransportOn ? CRGB::Red : CRGB::Black;
    leds[STREET_LED] = streetLightOn ? CRGB::Red : CRGB::Black; // Update street LED state
}

// Check button state and toggle wind effect
void checkButtonState() {
    uint32_t currentMillis = millis();

    // If the general timer is active, handle its behavior
    if (generalTimerActive) {
        // Disable windOn after 1 minute (60,000 ms)
        if (currentMillis - generalTimerStartTime >= windTime && windOn) {
            windOn = false; // Turn off wind effect
            
        }

        // Re-enable the button after 90 seconds (90,000 ms)
        if (currentMillis - generalTimerStartTime >= runTime) {
            hydrogenStorageFull = false; // Reset hydrogen storage full state
            electricityTransportOn = false; // Stop electricity transport
            generalTimerActive = false; // Stop the general timer
            buttonDisabled = false;    // Re-enable the button
            bool emptyPipe = false; 
            bool pipeEmpty = false;
            resetAllVariables(); // Reset all variables at startup

            digitalWrite(BUTTON_LED_PIN, HIGH); // Turn on the button LED
        }
        return; // Exit the function while the general timer is active
    }

    // Check the button state at regular intervals
    if (currentMillis - previousButtonCheckMillis >= BUTTON_CHECK_INTERVAL) {
        previousButtonCheckMillis = currentMillis;

        // If the button is pressed
        if (digitalRead(BUTTON_PIN) == LOW && !buttonDisabled) {
            digitalWrite(BUTTON_LED_PIN, LOW); // Turn on the button LED
            windOn = true; // Start the wind effect
            buttonDisabled = true; // Disable the button
            generalTimerActive = true; // Activate the general timer
            generalTimerStartTime = currentMillis; // Record the start time of the general timer
            
        }
    }
}
void resetAllVariables() {
    // Reset state variables
    windOn = false;
    solarOn = false;
    electricityProductionOn = false;
    electrolyserOn = false;
    hydrogenTransportOn = false;
    hydrogenProductionOn = false;
    hydrogenStorageOn = false;
    hydrogenStorageFull = false;
    h2ConsumptionOn = false;
    fabricationOn = false;
    electricityTransportOn = false;
    storageTransportOn = false;
    storagePowerstationOn = false;
    streetLightOn = false;
    emptyPipe = false;
    pipeEmpty = false;

    // Reset timers
    previousButtonCheckMillis = 0;
    previousMillisWind = 0;
    previousMillisSolar = 0;
    previousMillisElectricityProduction = 0;
    previousMillisElectrolyser = 0;
    previousMillisHydrogenTransport = 0;
    previousMillisHydrogenProduction = 0;
    previousMillisHydrogenStorage = 0;
    previousMillisHydrogenStorage2 = 0;
    previousMillisH2Consumption = 0;
    hydrogenStorageFullStartTime = 0;
    previousMillisElectricityTransport = 0;
    previousMillisStorageTransport = 0;
    previousMillisStoragePowerstation = 0;
    hydrogenStorageFullTimer = 0;

    // Reset segment indices
    windSegment = WIND_LED_START;
    solarSegment = SOLAR_LED_END;
    electricityProductionSegment = ELECTRICITY_PRODUCTION_LED_START;
    hydrogenTransportSegment = HYDROGEN_TRANSPORT_LED_START;
    hydrogenProductionSegment = HYDROGEN_PRODUCTION_LED_START;
    hydrogenStorageSegment1 = HYDROGEN_STORAGE1_LED_START;
    hydrogenStorageSegment2 = HYDROGEN_STORAGE2_LED_START;
    h2ConsumptionSegment = HYDROGEN_CONSUMPTION_LED_START;
    electricityTransportSegment = ELECTRICITY_TRANSPORT_LED_START;
    storageTransportSegment = STORAGE_TRANSPORT_LED_START;
    storagePowerstationSegment = STORAGE_POWERSTATION_LED_START;

    // Reset first-run flags
    firstRunWind = true;
    firstRunSolar = true;
    firstRunElectricityProduction = true;
    firstRunHydrogenProduction = true;
    firstRunHydrogenTransport = true;
    firstRunHydrogenStorage = true;
    firstRunHydrogenStorage2 = true;
    firstRunH2Consumption = true;
    firstRunElectricityTransport = true;
    firstRunStorageTransport = true;
    firstRunStoragePowerstation = true;

    // Reset button and timer states
    buttonDisabled = false;
    buttonDisableStartTime = 0;
    generalTimerStartTime = 0;
    generalTimerActive = false;
    storageTimerStarted = false;

   
}