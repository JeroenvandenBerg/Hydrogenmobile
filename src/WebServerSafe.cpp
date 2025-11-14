#include "../include/WebServerSafe.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "../include/Config.h"
#include "../include/SystemState.h"
#include <Arduino.h>
#include <FastLED.h>
#include "../include/logo_data_uri.h"
#include "../include/effects/EffectUtils.h"

// Use the global state defined in main.cpp
extern SystemState state;

// Forward declaration from main.cpp to reset program state
void resetAllVariables();

static AsyncWebServer server(80);
static Preferences prefs;
static Preferences programPrefs;

// Task to restart the ESP after sending the response
static void restartTask(void *pvParameters) {
    // small delay to allow the HTTP response to be sent
    vTaskDelay(200 / portTICK_PERIOD_MS);
    ESP.restart();
    vTaskDelete(NULL);
}

void initWebServerSafe() {
    // Start AP with a simple SSID. This is intentionally minimal and unsecured for local use only.
    WiFi.softAP("HydrogenDemo", "12345678");
    Serial.print("Web UI AP IP: ");
    Serial.println(WiFi.softAPIP());

    // Open preferences namespaces
    prefs.begin("led-config", false);
    programPrefs.begin("program", false);

    // Migrate program-level settings from legacy keys if needed
    if (!programPrefs.isKey("auto_start") && prefs.isKey("auto_start_enabled")) {
        bool legacyAuto = prefs.getBool("auto_start_enabled", false);
        programPrefs.putBool("auto_start", legacyAuto);
    }
    if (!programPrefs.isKey("h2_trans_delay_s") && prefs.isKey("h2_trans_delay_s")) {
        uint32_t legacyDelay = prefs.getUInt("h2_trans_delay_s", state.hydrogenTransportDelaySeconds);
        if (legacyDelay > 600) legacyDelay = 600;
        programPrefs.putUInt("h2_trans_delay_s", legacyDelay);
    }

    // Load all persisted segments with defaults from Config.h
    auto loadSegment = [](const char* startKey, const char* endKey, int defStart, int defEnd, int &outStart, int &outEnd) {
        int s = prefs.getInt(startKey, defStart);
        int e = prefs.getInt(endKey, defEnd);
        if (s < 0) s = 0;
        if (e >= NUM_LEDS) e = NUM_LEDS - 1;
        if (s > e) e = s;
        outStart = s;
        outEnd = e;
    };
    auto loadDir = [](const char* key, bool defVal) {
        return prefs.getBool(key, defVal);
    };
    auto loadEn = [](const char* key, bool defVal) {
        return prefs.getBool(key, defVal);
    };
    auto loadDelay = [](const char* key, int defVal) {
        int d = prefs.getInt(key, defVal);
        if (d < 1) d = 1; // minimum 1ms
        if (d > 10000) d = 10000; // max 10 seconds
        return d;
    };
    auto loadEffect3 = [](const char* key, int defVal) {
        int v = prefs.getInt(key, defVal);
        if (v < 0 || v > 2) v = defVal; // clamp to 0/1/2 for 3-option selects
        return v;
    };
    // Color helpers
    auto packColor = [](CRGB c) -> uint32_t {
        return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
    };
    auto unpackColor = [](uint32_t v) -> CRGB {
        return CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    };
    auto loadColor = [&](const char* key, CRGB def) {
        uint32_t defHex = packColor(def);
        uint32_t vv = prefs.getUInt(key, defHex);
        return unpackColor(vv);
    };
    auto colorToHex = [](CRGB c) -> String {
        char buf[8];
        sprintf(buf, "%02X%02X%02X", c.r, c.g, c.b);
        return String("#") + String(buf);
    };

    // Load segment names (with sensible defaults)
    auto loadName = [&](const char* key, const char* defVal) -> String {
        return prefs.getString(key, defVal);
    };
    state.windName = loadName("wind_name", "Wind");
    state.solarName = loadName("solar_name", "Solar");
    state.electricityProductionName = loadName("elec_prod_name", "Electricity Production");
    state.hydrogenProductionName = loadName("h2_prod_name", "Hydrogen Production");
    state.hydrogenTransportName = loadName("h2_trans_name", "Hydrogen Transport");
    state.hydrogenStorage1Name = loadName("h2_stor1_name", "Hydrogen Storage 1");
    state.hydrogenStorage2Name = loadName("h2_stor2_name", "Hydrogen Storage 2");
    state.h2ConsumptionName = loadName("h2_cons_name", "Hydrogen Consumption");
    state.fabricationName = loadName("fabr_name", "Fabrication");
    state.electricityTransportName = loadName("elec_tran_name", "Electricity Transport");
    state.storageTransportName = loadName("stor_tran_name", "Storage Transport");
    state.storagePowerstationName = loadName("stor_pow_name", "Storage Powerstation");

    loadSegment("wind_start", "wind_end", WIND_LED_START, WIND_LED_END, state.windSegmentStart, state.windSegmentEnd);
    loadSegment("solar_start", "solar_end", SOLAR_LED_START, SOLAR_LED_END, state.solarSegmentStart, state.solarSegmentEnd);
    loadSegment("elec_prod_s", "elec_prod_e", ELECTRICITY_PRODUCTION_LED_START, ELECTRICITY_PRODUCTION_LED_END, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd);
    loadSegment("h2_prod_s", "h2_prod_e", HYDROGEN_PRODUCTION_LED_START, HYDROGEN_PRODUCTION_LED_END, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd);
    loadSegment("h2_trans_s", "h2_trans_e", HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
    loadSegment("h2_stor1_s", "h2_stor1_e", HYDROGEN_STORAGE1_LED_START, HYDROGEN_STORAGE1_LED_END, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
    loadSegment("h2_stor2_s", "h2_stor2_e", HYDROGEN_STORAGE2_LED_START, HYDROGEN_STORAGE2_LED_END, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
    loadSegment("h2_cons_s", "h2_cons_e", HYDROGEN_CONSUMPTION_LED_START, HYDROGEN_CONSUMPTION_LED_END, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
    loadSegment("fabr_start", "fabr_end", FABRICATION_LED_START, FABRICATION_LED_END, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    loadSegment("elec_tran_s", "elec_tran_e", ELECTRICITY_TRANSPORT_LED_START, ELECTRICITY_TRANSPORT_LED_END, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
    loadSegment("stor_tran_s", "stor_tran_e", STORAGE_TRANSPORT_LED_START, STORAGE_TRANSPORT_LED_END, state.storageTransportSegmentStart, state.storageTransportSegmentEnd);
    loadSegment("stor_pow_s", "stor_pow_e", STORAGE_POWERSTATION_LED_START, STORAGE_POWERSTATION_LED_END, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);

    // Load directions with defaults that match historical behavior
    state.windDirForward = loadDir("wind_dir", true);
    state.solarDirForward = loadDir("solar_dir", false);
    state.electricityProductionDirForward = loadDir("elec_prod_dir", true);
    state.hydrogenTransportDirForward = loadDir("h2_trans_dir", true);
    state.hydrogenStorage1DirForward = loadDir("h2_stor1_dir", true);
    state.hydrogenStorage2DirForward = loadDir("h2_stor2_dir", true);
    state.h2ConsumptionDirForward = loadDir("h2_cons_dir", true);
    state.electricityTransportDirForward = loadDir("elec_tran_dir", true);
    state.storageTransportDirForward = loadDir("stor_tran_dir", true);
    state.storagePowerstationDirForward = loadDir("stor_pow_dir", true);

    // Load enabled flags
    state.windEnabled = loadEn("wind_en", true);
    state.solarEnabled = loadEn("solar_en", true);
    state.electricityProductionEnabled = loadEn("elec_prod_en", true);
    state.electrolyserEnabled = loadEn("electrolyser_en", true);
    state.hydrogenProductionEnabled = loadEn("h2_prod_en", true);
    state.hydrogenTransportEnabled = loadEn("h2_trans_en", true);
    state.hydrogenStorageEnabled = loadEn("h2_stor_en", true);
    state.h2ConsumptionEnabled = loadEn("h2_cons_en", true);
    state.fabricationEnabled = loadEn("fabr_en", true);
    state.electricityTransportEnabled = loadEn("elec_tran_en", true);
    state.storageTransportEnabled = loadEn("stor_tran_en", true);
    state.storagePowerstationEnabled = loadEn("stor_pow_en", true);

    // Load delays
    state.windDelay = loadDelay("wind_delay", LED_DELAY);
    state.solarDelay = loadDelay("solar_delay", LED_DELAY);
    state.electricityProductionDelay = loadDelay("elec_prod_delay", LED_DELAY);
    state.hydrogenTransportDelay = loadDelay("h2_trans_delay", LED_DELAY);
    state.hydrogenStorage1Delay = loadDelay("h2_stor1_delay", LED_DELAY);
    state.hydrogenStorage2Delay = loadDelay("h2_stor2_delay", LED_DELAY);
    state.h2ConsumptionDelay = loadDelay("h2_cons_delay", LED_DELAY);
    state.electricityTransportDelay = loadDelay("elec_tran_delay", LED_DELAY);
    state.storageTransportDelay = loadDelay("stor_tran_delay", LED_DELAY2);
    state.storagePowerstationDelay = loadDelay("stor_pow_delay", LED_DELAY2);

    // Load effect types
    // Load all effect types as 3-option: 0=Running, 1=Fire, 2=Fade
    state.windEffectType = loadEffect3("wind_eff", 0);
    state.solarEffectType = loadEffect3("solar_eff", 0);
    state.electricityProductionEffectType = loadEffect3("elec_prod_eff", 0);
    state.hydrogenTransportEffectType = loadEffect3("h2_trans_eff", 0);
    state.hydrogenStorage1EffectType = loadEffect3("h2_stor1_eff", 0);
    state.hydrogenStorage2EffectType = loadEffect3("h2_stor2_eff", 0);
    state.h2ConsumptionEffectType = loadEffect3("h2_cons_eff", 0);
    state.electricityTransportEffectType = loadEffect3("elec_tran_eff", 0);
    state.storageTransportEffectType = loadEffect3("stor_tran_eff", 0);
    state.storagePowerstationEffectType = loadEffect3("stor_pow_eff", 0);
    state.hydrogenProductionEffectType = loadEffect3("h2_prod_eff", 0);
    state.fabricationEffectType = loadEffect3("fabr_eff", 0);

    state.autoStartEnabled = programPrefs.getBool("auto_start", false);
    uint16_t transportDelaySec = static_cast<uint16_t>(programPrefs.getUInt(
        "h2_trans_delay_s",
        prefs.getUInt("h2_trans_delay_s", state.hydrogenTransportDelaySeconds)));
    if (!programPrefs.isKey("h2_trans_delay_s")) {
        programPrefs.putUInt("h2_trans_delay_s", transportDelaySec);
    }
    if (!prefs.isKey("h2_trans_delay_s")) {
        prefs.putUInt("h2_trans_delay_s", transportDelaySec);
    }
    if (transportDelaySec > 600) transportDelaySec = 600;
    state.hydrogenTransportDelaySeconds = static_cast<uint16_t>(transportDelaySec);

    // Load per-segment colors (defaults come from SystemState fields)
    state.windColor = loadColor("wind_color", state.windColor);
    state.solarColor = loadColor("solar_color", state.solarColor);
    state.electricityProductionColor = loadColor("elec_prod_color", state.electricityProductionColor);
    state.hydrogenProductionColor = loadColor("h2_prod_color", state.hydrogenProductionColor);
    state.hydrogenTransportColor = loadColor("h2_trans_color", state.hydrogenTransportColor);
    state.hydrogenStorage1Color = loadColor("h2_stor1_color", state.hydrogenStorage1Color);
    state.hydrogenStorage2Color = loadColor("h2_stor2_color", state.hydrogenStorage2Color);
    state.h2ConsumptionColor = loadColor("h2_cons_color", state.h2ConsumptionColor);
    state.fabricationColor = loadColor("fabr_color", state.fabricationColor);
    state.electricityTransportColor = loadColor("elec_tran_color", state.electricityTransportColor);
    state.storageTransportColor = loadColor("stor_tran_color", state.storageTransportColor);
    state.storagePowerstationColor = loadColor("stor_pow_color", state.storagePowerstationColor);

    // Load custom segments
    for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
        String prefix = "cust" + String(i) + "_";
        bool inUse = prefs.getBool((prefix + "inuse").c_str(), false);
        state.custom[i].inUse = inUse;
        if (!inUse) continue;
        state.custom[i].name = prefs.getString((prefix + "name").c_str(), ("Custom " + String(i+1)).c_str());
        int defS = 0, defE = 0;
        state.custom[i].start = prefs.getInt((prefix + "s").c_str(), defS);
        state.custom[i].end = prefs.getInt((prefix + "e").c_str(), defE);
        state.custom[i].dirForward = prefs.getBool((prefix + "dir").c_str(), true);
        state.custom[i].enabled = prefs.getBool((prefix + "en").c_str(), true);
        state.custom[i].delay = prefs.getInt((prefix + "delay").c_str(), LED_DELAY);
        state.custom[i].effectType = prefs.getInt((prefix + "eff").c_str(), 0);
        uint32_t cdef = 0xFFFFFF;
        uint32_t cval = prefs.getUInt((prefix + "color").c_str(), cdef);
        state.custom[i].color = CRGB((cval >> 16) & 0xFF, (cval >> 8) & 0xFF, cval & 0xFF);
        uint8_t tval = prefs.getUChar((prefix + "trig").c_str(), static_cast<uint8_t>(TriggerType::ALWAYS_ON));
        if (tval > 11) tval = 0;
        state.custom[i].trigger = static_cast<TriggerType>(tval);
        // runtime indices
        state.custom[i].firstRun = true;
        state.custom[i].segmentIndex = 0;
        state.custom[i].prevMillis = 0;
    }

    // One-time back-compat migration: previous mappings differed for Hydrogen Production and Fabrication
    // Guard with a flag so we don't remap new values on every boot
    if (!prefs.getBool("effects_v2", false)) {
        // Old Hydrogen Production: 0=Fade,1=Fire,2=Running => map to 0=Running,1=Fire,2=Fade
        if (prefs.isKey("h2_prod_eff")) {
            int old = prefs.getInt("h2_prod_eff", state.hydrogenProductionEffectType);
            int mapped = old;
            if (old == 2) mapped = 0;
            else if (old == 1) mapped = 1;
            else if (old == 0) mapped = 2;
            if (mapped != old) {
                prefs.putInt("h2_prod_eff", mapped);
            }
            state.hydrogenProductionEffectType = mapped;
        }
        // Old Fabrication: 0=Fire,1=Fade,2=Running => map to 0=Running,1=Fire,2=Fade
        if (prefs.isKey("fabr_eff")) {
            int old = prefs.getInt("fabr_eff", state.fabricationEffectType);
            int mapped = old;
            if (old == 2) mapped = 0;
            else if (old == 0) mapped = 1;
            else if (old == 1) mapped = 2;
            if (mapped != old) {
                prefs.putInt("fabr_eff", mapped);
            }
            state.fabricationEffectType = mapped;
        }
        prefs.putBool("effects_v2", true);
    }
    // Non-running segments direction and delay (for running option)
    state.hydrogenProductionDirForward = loadDir("h2_prod_dir", true);
    state.fabricationDirForward = loadDir("fabr_dir", true);
    state.hydrogenProductionDelay = loadDelay("h2_prod_delay", LED_DELAY);
    state.fabricationDelay = loadDelay("fabr_delay", LED_DELAY);

    // Serve root page with all segments
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String page = "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LED Segments</title>"
            "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:20px auto;padding:10px;}"
            ".logo{text-align:center;margin:20px 0;}"
            ".logo img{max-width:200px;height:auto;}"
            "h3{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:5px;}"
            ".segment{background:#f9f9f9;padding:10px;margin:10px 0;border-radius:5px;}"
            "input{width:60px;padding:5px;margin:3px;}"
            "select{margin:3px;}"
            "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;margin:5px;}"
            "button:hover{background:#45a049;}"
            ".restart{background:#d9534f;}"
            ".restart:hover{background:#c9302c;}"
            ".test{background:#5bc0de;padding:8px 12px;}"
            ".test:hover{background:#46b8da;}"
            ".stop{background:#f0ad4e;}"
            ".stop:hover{background:#ec971f;}"
            ".line2{display:grid;grid-template-columns:auto auto auto;gap:6px;align-items:center;}"
            ".line3{display:flex;gap:10px;align-items:center;flex-wrap:wrap;}"
            "</style></head><body>"
            "<div class='logo'><img src='" + String(LOGO_DATA_URI) + "' alt='OakZo Logo'></div>"
            "<h3>LED Segment Settings</h3>"
            "<div style='margin:10px 0;'>"
            "<a href='/'><button type='button'>Settings</button></a>"
            "<a href='/status'><button type='button'>Status</button></a>"
            "</div>";
        
        if (state.testMode) {
            page += "<div style='background:#fff3cd;padding:15px;border-radius:5px;margin:10px 0;border:2px solid #ffc107;'>"
                    "<b>TEST MODE ACTIVE</b><br>Testing segment " + String(state.testSegmentStart) + "-" + String(state.testSegmentEnd) + "<br>"
                    "<form method='POST' action='/stoptest' style='display:inline;'>"
                    "<button type='submit' class='stop'>Stop Test</button></form></div>";
        }
        
    page += "<form id='saveForm' method=\"POST\" action=\"/update\">";
    page += "<div class='segment'><b>Program Options</b><br>"
        "Auto-start program (disables manual button): "
        "<input type='checkbox' name='auto_start' value='1" + String(state.autoStartEnabled ? "' checked" : "'") + ">"
        "<br>Hydrogen transport delay after electrolyser (seconds): "
        "<input type='number' name='h2_trans_delay_s' min='0' max='600' value='" + String(state.hydrogenTransportDelaySeconds) + "'>"
        "</div>";
        
    // Helper lambda to create segment row without trigger dropdown
    auto colorToHexLocal = [](CRGB c) -> String { char buf[8]; sprintf(buf, "%02X%02X%02X", c.r, c.g, c.b); return String("#") + String(buf); };
    auto addSegmentDir = [&](const char* nameLabel, const char* nameKey, const char* startName, const char* endName,
                 const char* dirName, const char* enName, const char* delayName, const char* effName,
                 const char* colorName, CRGB colorVal,
                 int startVal, int endVal, bool dirVal, bool enVal, int delayVal, int effVal) {
            page += "<div class='segment'><b>" + String(nameLabel) + "</b><br>"
                // Name on its own line
                "Name: <input type='text' name='" + String(nameKey) + "' value='" + String(nameLabel) + "' maxlength='32' style='width:180px;'>" \
                "<br>"
                // Line 1: start and end
                "Start: <input id='" + String(startName) + "' type='number' name='" + String(startName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(startVal) + ">"
                " End: <input id='" + String(endName) + "' type='number' name='" + String(endName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(endVal) + ">" \
                "<br>"
                // Line 2: effect, direction, delay (compact grid)
                "<div class='line2'>"
                "<span>Effect: <select name='" + String(effName) + "' onchange='toggleDirDelay(this,\"" + String(dirName) + "\",\"" + String(delayName) + "\",\"" + String(colorName) + "\")'>"
                "<option value='0'" + String(effVal==0 ? " selected" : "") + ">Running</option>"
                "<option value='1'" + String(effVal==1 ? " selected" : "") + ">Fire</option>"
                "<option value='2'" + String(effVal==2 ? " selected" : "") + ">Fade</option>"
                "</select></span>"
                " <span id='ctrl_" + String(dirName) + "' style='display:" + String(effVal==0 ? "inline" : "none") + ";'>Dir: <select name='" + String(dirName) + "'>"
                "<option value='1'" + String(dirVal ? " selected" : "") + ">Forward</option>"
                "<option value='0'" + String(!dirVal ? " selected" : "") + ">Reverse</option>"
                "</select></span>"
                " <span id='ctrl_" + String(delayName) + "' style='display:" + String((effVal==0 || effVal==2) ? "inline" : "none") + ";'>Delay(ms): <input type='number' name='" + String(delayName) + "' min=1 max=10000 value=" + String(delayVal) + " style='width:60px;'></span>"
                "</div>" \
                "<br>"
                // Line 3: color and enabled
                "<div class='line3'>"
                " <span id='ctrl_" + String(colorName) + "' style='display:" + String((effVal==0 || effVal==2) ? "inline" : "none") + ";'>"
                " Color: <input type='color' name='" + String(colorName) + "' value='" + colorToHexLocal(colorVal) + "'>"
                "</span>"
                + " <span>Enabled: <input type='checkbox' name='" + String(enName) + "' value='1" + String(enVal ? "' checked" : "'") + "></span>"
                "<button type='button' class='test' onclick=\"testSegment('" + String(startName) + "','" + String(endName) + "','" + String(dirName) + "','" + String(effName) + "','" + String(delayName) + "','" + String(colorName) + "')\">Test</button>"
                "</div>"
                "</div>";
        };
    auto addSegmentSimple = [&](const char* nameLabel, const char* nameKey, const char* startName, const char* endName, const char* enName, const char* effName, const char* dirName, const char* delayName, const char* colorName, CRGB colorVal, int startVal, int endVal, bool enVal, int effVal, bool dirVal, int delayVal) {
            page += "<div class='segment'><b>" + String(nameLabel) + "</b><br>"
                // Name on its own line
                "Name: <input type='text' name='" + String(nameKey) + "' value='" + String(nameLabel) + "' maxlength='32' style='width:180px;'>" \
                "<br>"
                // Line 1: start and end
                "Start: <input id='" + String(startName) + "' type='number' name='" + String(startName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(startVal) + ">"
                " End: <input id='" + String(endName) + "' type='number' name='" + String(endName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(endVal) + ">" \
                "<br>"
        // Line 2: effect, direction, delay (compact grid)
        "<div class='line2'>"
        "<span>Effect: <select name='" + String(effName) + "' onchange='toggleDirDelay(this,\"" + String(dirName) + "\",\"" + String(delayName) + "\",\"" + String(colorName) + "\")'>"
        "<option value='0'" + String(effVal==0 ? " selected" : "") + ">Running</option>"
        "<option value='1'" + String(effVal==1 ? " selected" : "") + ">Fire</option>"
        "<option value='2'" + String(effVal==2 ? " selected" : "") + ">Fade</option>"
        "</select></span>"
        " <span id='ctrl_" + String(dirName) + "' style='display:" + String(effVal==0 ? "inline" : "none") + ";'>Dir: <select name='" + String(dirName) + "'>"
        "<option value='1'" + String(dirVal ? " selected" : "") + ">Forward</option>"
        "<option value='0'" + String(!dirVal ? " selected" : "") + ">Reverse</option>"
        "</select></span>"
        " <span id='ctrl_" + String(delayName) + "' style='display:" + String((effVal==0 || effVal==2) ? "inline" : "none") + ";'>Delay(ms): <input type='number' name='" + String(delayName) + "' min=1 max=10000 value=" + String(delayVal) + " style='width:60px;'></span>"
        "</div>" \
                "<br>"
        // Line 3: color and enabled
        "<div class='line3'>"
        " <span id='ctrl_" + String(colorName) + "' style='display:" + String((effVal==0 || effVal==2) ? "inline" : "none") + ";'>"
        " Color: <input type='color' name='" + String(colorName) + "' value='" + colorToHexLocal(colorVal) + "'>"
        "</span>"
        + " <span>Enabled: <input type='checkbox' name='" + String(enName) + "' value='1" + String(enVal ? "' checked" : "'") + "></span>"
    "<button type='button' class='test' onclick=\"testSegment('" + String(startName) + "','" + String(endName) + "','" + String(dirName) + "','" + String(effName) + "','" + String(delayName) + "','" + String(colorName) + "')\">Test</button>"
        "</div>"
                "</div>";
        };
        
    addSegmentDir(state.windName.c_str(), "wind_name", "wind_start", "wind_end", "wind_dir", "wind_en", "wind_delay", "wind_eff", "wind_color", state.windColor, state.windSegmentStart, state.windSegmentEnd, state.windDirForward, state.windEnabled, state.windDelay, state.windEffectType);
    addSegmentDir(state.solarName.c_str(), "solar_name", "solar_start", "solar_end", "solar_dir", "solar_en", "solar_delay", "solar_eff", "solar_color", state.solarColor, state.solarSegmentStart, state.solarSegmentEnd, state.solarDirForward, state.solarEnabled, state.solarDelay, state.solarEffectType);
    addSegmentDir(state.electricityProductionName.c_str(), "elec_prod_name", "elec_prod_s", "elec_prod_e", "elec_prod_dir", "elec_prod_en", "elec_prod_delay", "elec_prod_eff", "elec_prod_color", state.electricityProductionColor, state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd, state.electricityProductionDirForward, state.electricityProductionEnabled, state.electricityProductionDelay, state.electricityProductionEffectType);
    addSegmentDir(state.hydrogenTransportName.c_str(), "h2_trans_name", "h2_trans_s", "h2_trans_e", "h2_trans_dir", "h2_trans_en", "h2_trans_delay", "h2_trans_eff", "h2_trans_color", state.hydrogenTransportColor, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd, state.hydrogenTransportDirForward, state.hydrogenTransportEnabled, state.hydrogenTransportDelay, state.hydrogenTransportEffectType);
    addSegmentDir(state.hydrogenStorage1Name.c_str(), "h2_stor1_name", "h2_stor1_s", "h2_stor1_e", "h2_stor1_dir", "h2_stor_en", "h2_stor1_delay", "h2_stor1_eff", "h2_stor1_color", state.hydrogenStorage1Color, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd, state.hydrogenStorage1DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage1Delay, state.hydrogenStorage1EffectType);
    addSegmentDir(state.hydrogenStorage2Name.c_str(), "h2_stor2_name", "h2_stor2_s", "h2_stor2_e", "h2_stor2_dir", "h2_stor_en", "h2_stor2_delay", "h2_stor2_eff", "h2_stor2_color", state.hydrogenStorage2Color, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd, state.hydrogenStorage2DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage2Delay, state.hydrogenStorage2EffectType);
    addSegmentDir(state.h2ConsumptionName.c_str(), "h2_cons_name", "h2_cons_s", "h2_cons_e", "h2_cons_dir", "h2_cons_en", "h2_cons_delay", "h2_cons_eff", "h2_cons_color", state.h2ConsumptionColor, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd, state.h2ConsumptionDirForward, state.h2ConsumptionEnabled, state.h2ConsumptionDelay, state.h2ConsumptionEffectType);
    addSegmentSimple(state.fabricationName.c_str(), "fabr_name", "fabr_start", "fabr_end", "fabr_en", "fabr_eff", "fabr_dir", "fabr_delay", "fabr_color", state.fabricationColor, state.fabricationSegmentStart, state.fabricationSegmentEnd, state.fabricationEnabled, state.fabricationEffectType, state.fabricationDirForward, state.fabricationDelay);
    addSegmentDir(state.electricityTransportName.c_str(), "elec_tran_name", "elec_tran_s", "elec_tran_e", "elec_tran_dir", "elec_tran_en", "elec_tran_delay", "elec_tran_eff", "elec_tran_color", state.electricityTransportColor, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd, state.electricityTransportDirForward, state.electricityTransportEnabled, state.electricityTransportDelay, state.electricityTransportEffectType);
    addSegmentDir(state.storageTransportName.c_str(), "stor_tran_name", "stor_tran_s", "stor_tran_e", "stor_tran_dir", "stor_tran_en", "stor_tran_delay", "stor_tran_eff", "stor_tran_color", state.storageTransportColor, state.storageTransportSegmentStart, state.storageTransportSegmentEnd, state.storageTransportDirForward, state.storageTransportEnabled, state.storageTransportDelay, state.storageTransportEffectType);
        addSegmentDir(state.storagePowerstationName.c_str(), "stor_pow_name", "stor_pow_s", "stor_pow_e", "stor_pow_dir", "stor_pow_en", "stor_pow_delay", "stor_pow_eff", "stor_pow_color", state.storagePowerstationColor, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd, state.storagePowerstationDirForward, state.storagePowerstationEnabled, state.storagePowerstationDelay, state.storagePowerstationEffectType);

        // Custom segments section
        page += "<h3>Custom Segments</h3>";
        // Add button if capacity available
        bool hasFree = false; 
        for (int i=0;i<SystemState::MAX_CUSTOM_SEGMENTS;++i){ 
            if(!state.custom[i].inUse){ 
                hasFree=true; 
                break;
            }
        }
        
        if (hasFree) {
            page += "<button type='button' onclick=\"addCustomSegment()\">Add Custom Segment</button>";
        }
        
        for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
            auto &cs = state.custom[i];
            if (!cs.inUse) continue;
            String idx = String(i);
            // Render custom segment controls (same 3-line layout) + remove button
            page += "<div class='segment'><b>" + cs.name + "</b><br>";
            page += "Name: <input type='text' name='cust" + idx + "_name' value='" + cs.name + "' maxlength='32' style='width:180px;'>";
            page += "<br>Start: <input id='cust" + idx + "_s' type='number' name='cust" + idx + "_s' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(cs.start) + ">";
            page += " End: <input id='cust" + idx + "_e' type='number' name='cust" + idx + "_e' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(cs.end) + ">";
            page += "<br><div class='line2'>";
            page += "<span>Effect: <select name='cust" + idx + "_eff' onchange=\"toggleDirDelay(this,'cust" + idx + "_dir','cust" + idx + "_delay','cust" + idx + "_color')\">";
            page += "<option value='0" + String(cs.effectType==0?"' selected":"'") + ">Running</option>";
            page += "<option value='1" + String(cs.effectType==1?"' selected":"'") + ">Fire</option>";
            page += "<option value='2" + String(cs.effectType==2?"' selected":"'") + ">Fade</option>";
            page += "</select></span>";
            page += " <span id='ctrl_cust" + idx + "_dir' style='display:" + String(cs.effectType==0?"inline":"none") + ";'>Dir: <select name='cust" + idx + "_dir'>";
            page += "<option value='1" + String(cs.dirForward?"' selected":"'") + ">Forward</option>";
            page += "<option value='0" + String(!cs.dirForward?"' selected":"'") + ">Reverse</option>";
            page += "</select></span>";
            page += " <span id='ctrl_cust" + idx + "_delay' style='display:" + String((cs.effectType==0||cs.effectType==2)?"inline":"none") + ";'>Delay(ms): <input type='number' name='cust" + idx + "_delay' min=1 max=10000 value=" + String(cs.delay) + " style='width:60px;'></span>";
            page += "</div><br>";
            page += "<div class='line3'>";
            page += " <span id='ctrl_cust" + idx + "_color' style='display:" + String((cs.effectType==0||cs.effectType==2)?"inline":"none") + ";'> Color: <input type='color' name='cust" + idx + "_color' value='" + colorToHexLocal(cs.color) + "'></span>";
            page += " <span>Enabled: <input type='checkbox' name='cust" + idx + "_en' value='1" + String(cs.enabled?"' checked":"'") + "></span>";
            page += "<button type='button' class='test' onclick=\"testSegment('cust" + idx + "_s','cust" + idx + "_e','cust" + idx + "_dir','cust" + idx + "_eff','cust" + idx + "_delay','cust" + idx + "_color')\">Test</button>";
            page += "<button type='button' class='stop' style='margin-left:8px;' onclick=\"removeCustomSegment(" + idx + ")\">Remove</button>";
            page += "</div></div>";
        }

        // Non-segment control: Electrolyser enable
        page += String("<div class='segment'><b>Electrolyser</b><br> Enabled: ") +
            "<input type='checkbox' name='electrolyser_en' value='1'" + String(state.electrolyserEnabled ? " checked" : "") + ">" +
            "</div>";
        
        page += "<button type='submit'>Save All Settings</button></form><hr>"
            "<script>\n"
            "function addCustomSegment(){\n"
            "  fetch('/add_custom',{method:'POST'})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Failed to add custom segment'));\n"
            "}\n"
            "function removeCustomSegment(id){\n"
            "  const body=new URLSearchParams({id:id}).toString();\n"
            "  fetch('/remove_custom',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Failed to remove custom segment'));\n"
            "}\n"
            "function toggleDirDelay(sel,dirName,delayName,colorName){\n"
            "  const v = sel.value;\n"
            "  const dirCtrl = document.getElementById('ctrl_'+dirName);\n"
            "  const delayCtrl = document.getElementById('ctrl_'+delayName);\n"
            "  const colorCtrl = document.getElementById('ctrl_'+colorName);\n"
            "  // Direction only for Running (0)\n"
            "  if(dirCtrl) dirCtrl.style.display = (v==='0') ? 'inline' : 'none';\n"
            "  // Delay for Running (0) and Fade (2)\n"
            "  if(delayCtrl) delayCtrl.style.display = (v==='0' || v==='2') ? 'inline' : 'none';\n"
            "  // Color for Running (0) and Fade (2)\n"
            "  if(colorCtrl) colorCtrl.style.display = (v==='0' || v==='2') ? 'inline' : 'none';\n"
            "}\n"
            "function testSegment(startName,endName,dirName,effName,delayName,colorName){\n"
            "  const s=document.getElementById(startName).value;\n"
            "  const e=document.getElementById(endName).value;\n"
            "  let d='1';\n"
            "  if(dirName){ const sel=document.querySelector(\"select[name='\"+dirName+\"']\"); if(sel){ d=sel.value; } }\n"
            "  let eff='0';\n"
            "  if(effName){ const effSel=document.querySelector(\"select[name='\"+effName+\"']\"); if(effSel){ eff=effSel.value; } }\n"
            "  let delay='50';\n"
            "  if(delayName){ const delayInput=document.querySelector(\"input[name='\"+delayName+\"']\"); if(delayInput){ delay=delayInput.value; } }\n"
            "  let color='#FFFFFF';\n"
            "  if(colorName){ const colorInput=document.querySelector(\"input[name='\"+colorName+\"']\"); if(colorInput){ color=colorInput.value; } }\n"
            "  const body=new URLSearchParams({start:s,end:e,dir:d,eff:eff,delay:delay,color:color}).toString();\n"
            "  fetch('/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Test request failed'));\n"
            "}\n"
            
            "</script>"
            "<form method='POST' action='/restart' onsubmit=\"return confirm('Restart the device?')\">"
            "<button type='submit' class='restart'>Restart ESP</button></form></body></html>";
        
        request->send(200, "text/html", page);
    });

        // Trigger configuration view removed; triggers now fixed in firmware defaults.

        // Status page - shows current trigger states
        server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
            String page = "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Trigger Status</title>"
                "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:20px auto;padding:10px;}"
                ".logo{text-align:center;margin:20px 0;}"
                ".logo img{max-width:200px;height:auto;}"
                "h3{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:5px;}"
                ".trigger{background:#f9f9f9;padding:10px;margin:10px 0;border-radius:5px;display:flex;justify-content:space-between;align-items:center;}"
                ".status{padding:5px 15px;border-radius:4px;font-weight:bold;}"
                ".status.on{background:#4CAF50;color:white;}"
                ".status.off{background:#ccc;color:#666;}"
                "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;margin:5px;}"
                "button.reset{background:#d9534f;}"
                "button.reset:hover{background:#c9302c;}"
                "button:hover{background:#45a049;}"
                "</style>"
                "<script>"
                "function refresh(){"
                "  window.location.reload();"
                "}"
                "setInterval(refresh, 2000);" // Auto-refresh every 2 seconds
                "</script>"
                "</head><body>"
                "<div class='logo'><img src='" + String(LOGO_DATA_URI) + "' alt='OakZo Logo'></div>"
                "<h3>Trigger Status Monitor</h3>"
                "<div style='margin:10px 0;'>"
                "<a href='/'><button type='button'>Settings</button></a>"
                "<a href='/status'><button type='button'>Status</button></a>"
                "</div>"
                "<p>Live status of all trigger conditions (auto-refreshes every 2 seconds):</p>";
          page += "<div style='margin:15px 0;'>"
                 "<form method='POST' action='/reset_loop' onsubmit=\"return confirm('Reset the program loop?');\" style='display:inline;'>"
                 "<button type='submit' class='reset'>Reset Program Loop</button>"
                 "</form>"
                 "</div>";
        
            // Helper to show trigger status using EffectUtils::isTriggerActive
            auto showTriggerStatus = [&](const char* label, TriggerType trigger) {
                bool isActive = EffectUtils::isTriggerActive(state, trigger);
                page += "<div class='trigger'><span>" + String(label) + "</span>";
                page += "<span class='status " + String(isActive ? "on" : "off") + "'>" + String(isActive ? "ACTIVE" : "INACTIVE") + "</span></div>";
            };
        
            showTriggerStatus("Wind Trigger", TriggerType::WIND);
            showTriggerStatus("Solar Trigger (uses Wind)", TriggerType::WIND);  // Solar uses Wind trigger by default
            showTriggerStatus("Electricity Production", TriggerType::ELECTRICITY_PROD);
            showTriggerStatus("Electrolyser", TriggerType::ELECTROLYSER);
            showTriggerStatus("Hydrogen Transport", TriggerType::HYDROGEN_TRANSPORT);
            showTriggerStatus("Hydrogen Storage", TriggerType::HYDROGEN_STORAGE);
            showTriggerStatus("H2 Consumption", TriggerType::H2_CONSUMPTION);
            showTriggerStatus("Fabrication", TriggerType::FABRICATION);
            showTriggerStatus("Electricity Transport", TriggerType::ELECTRICITY_TRANSPORT);
            showTriggerStatus("Storage Transport", TriggerType::STORAGE_TRANSPORT);
            showTriggerStatus("Storage Powerstation", TriggerType::STORAGE_POWERSTATION);
        
            page += "</body></html>";
            request->send(200, "text/html", page);
        });

        server.on("/reset_loop", HTTP_POST, [](AsyncWebServerRequest *request){
            resetAllVariables();
            state.windOn = false;
            state.buttonDisabled = false;
            state.generalTimerActive = false;
            digitalWrite(BUTTON_LED_PIN, HIGH);
            request->redirect("/status");
        });

        // Add a custom segment slot
        server.on("/add_custom", HTTP_POST, [](AsyncWebServerRequest *request){
            bool added = false;
            for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
                if (!state.custom[i].inUse) {
                    state.custom[i].inUse = true;
                    state.custom[i].name = String("Custom ") + String(i+1);
                    state.custom[i].start = 0;
                    state.custom[i].end = min(9, NUM_LEDS-1);
                    state.custom[i].dirForward = true;
                    state.custom[i].enabled = true;
                    state.custom[i].delay = LED_DELAY;
                    state.custom[i].effectType = 0;
                    state.custom[i].color = CRGB::White;
                    state.custom[i].trigger = TriggerType::ALWAYS_ON;
                    // Initialize runtime fields
                    state.custom[i].firstRun = true;
                    state.custom[i].segmentIndex = 0;
                    state.custom[i].prevMillis = 0;
                    String p = String("cust") + String(i) + "_";
                    prefs.putBool((p+"inuse").c_str(), true);
                    prefs.putString((p+"name").c_str(), state.custom[i].name);
                    prefs.putInt((p+"s").c_str(), state.custom[i].start);
                    prefs.putInt((p+"e").c_str(), state.custom[i].end);
                    prefs.putBool((p+"dir").c_str(), state.custom[i].dirForward);
                    prefs.putBool((p+"en").c_str(), state.custom[i].enabled);
                    prefs.putInt((p+"delay").c_str(), state.custom[i].delay);
                    prefs.putInt((p+"eff").c_str(), state.custom[i].effectType);
                    prefs.putUInt((p+"color").c_str(), 0xFFFFFF);
                    prefs.putUChar((p+"trig").c_str(), static_cast<uint8_t>(state.custom[i].trigger));
                    added = true;
                    break;
                }
            }
            if (added) {
                request->send(200, "text/plain", "OK");
            } else {
                request->send(400, "text/plain", "No free slots");
            }
        });

        // Remove a custom segment slot
        server.on("/remove_custom", HTTP_POST, [](AsyncWebServerRequest *request){
            if (!request->hasParam("id", true)) { request->send(400, "text/plain", "Missing id"); return; }
            int i = request->getParam("id", true)->value().toInt();
            if (i < 0 || i >= SystemState::MAX_CUSTOM_SEGMENTS) { request->send(400, "text/plain", "Bad id"); return; }
            state.custom[i].inUse = false;
            String p = String("cust") + String(i) + "_";
            prefs.putBool((p+"inuse").c_str(), false);
            // Clear LEDs for visual feedback
            EffectUtils::clearRange(state, state.custom[i].start, state.custom[i].end);
            request->redirect("/");
        });

    // Handle update for all segments
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
        // Helper to get and validate segment parameters
        auto getSegment = [&](const char* startName, const char* endName, int &outStart, int &outEnd) -> bool {
            if (!request->hasParam(startName, true) || !request->hasParam(endName, true)) return false;
            int s = request->getParam(startName, true)->value().toInt();
            int e = request->getParam(endName, true)->value().toInt();
            if (s < 0 || s >= NUM_LEDS || e < 0 || e >= NUM_LEDS || s > e) return false;
            outStart = s;
            outEnd = e;
            return true;
        };
        auto getName = [&](const char* key, String &out) -> bool {
            if (!request->hasParam(key, true)) return false;
            String v = request->getParam(key, true)->value();
            v.trim();
            if (v.length() > 32) v = v.substring(0, 32);
            out = v;
            return true;
        };
        auto getDir = [&](const char* dirName, bool &outDir) -> bool {
            if (!request->hasParam(dirName, true)) return false;
            String v = request->getParam(dirName, true)->value();
            outDir = (v == "1");
            return true;
        };
        auto getCheckbox = [&](const char* name) -> bool {
            // Checkboxes are sent only when checked; treat missing as false
            if (!request->hasParam(name, true)) return false;
            String v = request->getParam(name, true)->value();
            return (v == "1" || v == "on" || v == "true");
        };
        auto getDelay = [&](const char* name, int &outDelay) -> bool {
            if (!request->hasParam(name, true)) return false;
            int d = request->getParam(name, true)->value().toInt();
            if (d < 1 || d > 10000) return false;
            outDelay = d;
            return true;
        };
        auto getDelaySeconds = [&](const char* name, uint16_t &outSeconds) -> bool {
            if (!request->hasParam(name, true)) return false;
            int v = request->getParam(name, true)->value().toInt();
            if (v < 0 || v > 600) return false;
            outSeconds = static_cast<uint16_t>(v);
            return true;
        };
        auto getEffect3 = [&](const char* name, int &outEff) -> bool {
            if (!request->hasParam(name, true)) return false;
            int v = request->getParam(name, true)->value().toInt();
            if (v < 0 || v > 2) return false;
            outEff = v;
            return true;
        };

    int ws, we, ss, se, eps, epe, hts, hte, h1s, h1e, h2s, h2e, hcs, hce, fs, fe, ets, ete, sts, ste, sps, spe;
    bool wdir, sdir, epdir, htdir, h1dir, h2dir, hcdir, etdir, stdir, spdir, fbdir;
    int wdly, sdly, epdly, htdly, h1dly, h2dly, hcdly, etdly, stdly, spdly, fbdly;
    int weff, seff, epeff, hteff, h1eff, h2eff, hceff, eteff, steff, speff;
    int fbeff; // fabrication effect
    uint32_t wcol, scol, epcol, htcol, hs1col, hs2col, hccol, fbcol, etcol, stcol, spcol;
    String wname, sname, epname, htname, hs1name, hs2name, hcname, fbname, etname, stname, spname;
    uint16_t h2delaySeconds;
        
        if (!getSegment("wind_start", "wind_end", ws, we) ||
            !getSegment("solar_start", "solar_end", ss, se) ||
            !getSegment("elec_prod_s", "elec_prod_e", eps, epe) ||
            !getSegment("h2_trans_s", "h2_trans_e", hts, hte) ||
            !getSegment("h2_stor1_s", "h2_stor1_e", h1s, h1e) ||
            !getSegment("h2_stor2_s", "h2_stor2_e", h2s, h2e) ||
            !getSegment("h2_cons_s", "h2_cons_e", hcs, hce) ||
            !getSegment("fabr_start", "fabr_end", fs, fe) ||
            !getSegment("elec_tran_s", "elec_tran_e", ets, ete) ||
            !getSegment("stor_tran_s", "stor_tran_e", sts, ste) ||
            !getSegment("stor_pow_s", "stor_pow_e", sps, spe) ||
            !getDir("wind_dir", wdir) ||
            !getDir("solar_dir", sdir) ||
            !getDir("elec_prod_dir", epdir) ||
            !getDir("h2_trans_dir", htdir) ||
            !getDir("h2_stor1_dir", h1dir) ||
            !getDir("h2_stor2_dir", h2dir) ||
            !getDir("h2_cons_dir", hcdir) ||
            !getDir("elec_tran_dir", etdir) ||
            !getDir("stor_tran_dir", stdir) ||
            !getDir("stor_pow_dir", spdir) ||
            !getDir("fabr_dir", fbdir) ||
            !getEffect3("wind_eff", weff) ||
            !getEffect3("solar_eff", seff) ||
            !getEffect3("elec_prod_eff", epeff) ||
            !getEffect3("h2_trans_eff", hteff) ||
            !getEffect3("h2_stor1_eff", h1eff) ||
            !getEffect3("h2_stor2_eff", h2eff) ||
            !getEffect3("h2_cons_eff", hceff) ||
            !getEffect3("elec_tran_eff", eteff) ||
            !getEffect3("stor_tran_eff", steff) ||
            !getEffect3("stor_pow_eff", speff) ||
            !getEffect3("fabr_eff", fbeff) ||
            // Colors must be present when saving
            !request->hasParam("wind_color", true) ||
            !request->hasParam("solar_color", true) ||
            !request->hasParam("elec_prod_color", true) ||
            !request->hasParam("h2_trans_color", true) ||
            !request->hasParam("h2_stor1_color", true) ||
            !request->hasParam("h2_stor2_color", true) ||
            !request->hasParam("h2_cons_color", true) ||
            !request->hasParam("fabr_color", true) ||
            !request->hasParam("elec_tran_color", true) ||
            !request->hasParam("stor_tran_color", true) ||
            !request->hasParam("stor_pow_color", true) ||
            !getDelay("wind_delay", wdly) ||
            !getDelay("solar_delay", sdly) ||
            !getDelay("elec_prod_delay", epdly) ||
            !getDelay("h2_trans_delay", htdly) ||
            !getDelay("h2_stor1_delay", h1dly) ||
            !getDelay("h2_stor2_delay", h2dly) ||
            !getDelay("h2_cons_delay", hcdly) ||
            !getDelay("elec_tran_delay", etdly) ||
            !getDelay("stor_tran_delay", stdly) ||
            !getDelay("stor_pow_delay", spdly) ||
            !getDelay("fabr_delay", fbdly) ||
            !getDelaySeconds("h2_trans_delay_s", h2delaySeconds) ||
            !getName("wind_name", wname) ||
            !getName("solar_name", sname) ||
            !getName("elec_prod_name", epname) ||
            !getName("h2_trans_name", htname) ||
            !getName("h2_stor1_name", hs1name) ||
            !getName("h2_stor2_name", hs2name) ||
            !getName("h2_cons_name", hcname) ||
            !getName("fabr_name", fbname) ||
            !getName("elec_tran_name", etname) ||
            !getName("stor_tran_name", stname) ||
            !getName("stor_pow_name", spname)) {
            request->send(400, "text/plain", "Missing or invalid parameters");
            return;
        }

        // Parse colors (#RRGGBB)
        auto parseHexColor = [&](const char* name, uint32_t &out) -> bool {
            String s = request->getParam(name, true)->value();
            if (s.length() != 7 || s[0] != '#') return false;
            char *endptr = nullptr;
            String hex = s.substring(1);
            out = strtoul(hex.c_str(), &endptr, 16);
            return (endptr && *endptr == '\0');
        };
        if (!parseHexColor("wind_color", wcol) ||
            !parseHexColor("solar_color", scol) ||
            !parseHexColor("elec_prod_color", epcol) ||
            !parseHexColor("h2_trans_color", htcol) ||
            !parseHexColor("h2_stor1_color", hs1col) ||
            !parseHexColor("h2_stor2_color", hs2col) ||
            !parseHexColor("h2_cons_color", hccol) ||
            !parseHexColor("fabr_color", fbcol) ||
            !parseHexColor("elec_tran_color", etcol) ||
            !parseHexColor("stor_tran_color", stcol) ||
            !parseHexColor("stor_pow_color", spcol)) {
            request->send(400, "text/plain", "Invalid color format");
            return;
        }

        // Checkboxes (missing means false)
        bool wen  = getCheckbox("wind_en");
        bool sen  = getCheckbox("solar_en");
        bool epen = getCheckbox("elec_prod_en");
        bool hten = getCheckbox("h2_trans_en");
        bool hsen = getCheckbox("h2_stor_en");
        bool hcen = getCheckbox("h2_cons_en");
        bool fben = getCheckbox("fabr_en");
        bool eten = getCheckbox("elec_tran_en");
        bool sten = getCheckbox("stor_tran_en");
        bool spen = getCheckbox("stor_pow_en");
        bool elyen = getCheckbox("electrolyser_en");
        bool autoStart = getCheckbox("auto_start");

        // Build a temporary list of enabled segments (built-ins + in-use customs) to check for overlap BEFORE saving anything
        struct RangeItem { String name; int s; int e; };
        RangeItem ranges[20];
        int rc = 0;
        auto addRange = [&](const String &nm, int s, int e, bool en){ if(en && rc < 20){ ranges[rc++] = RangeItem{nm, s, e}; } };
        // Built-ins
        addRange(wname, ws, we, wen);
        addRange(sname, ss, se, sen);
        addRange(epname, eps, epe, epen);
        addRange(htname, hts, hte, hten);
        addRange(hs1name, h1s, h1e, hsen);
        addRange(hs2name, h2s, h2e, hsen);
        addRange(hcname, hcs, hce, hcen);
        addRange(fbname, fs, fe, fben);
        addRange(etname, ets, ete, eten);
        addRange(stname, sts, ste, sten);
        addRange(spname, sps, spe, spen);

        // Parse custom segments into temporaries for validation
        struct TmpCustom { bool present=false; String name; int s=0; int e=0; bool dir=true; bool en=true; int dly=LED_DELAY; int eff=0; uint32_t col=0xFFFFFF; };
        TmpCustom tmpCustoms[SystemState::MAX_CUSTOM_SEGMENTS];
        for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
            if (!state.custom[i].inUse) continue;
            String idx = String(i);
            auto hasParam = [&](const String &k){ return request->hasParam(k.c_str(), true); };
            String nameKey = "cust" + idx + "_name";
            String sKey = "cust" + idx + "_s";
            String eKey = "cust" + idx + "_e";
            String dirKey = "cust" + idx + "_dir";
            String enKey = "cust" + idx + "_en";
            String dlyKey = "cust" + idx + "_delay";
            String effKey = "cust" + idx + "_eff";
            String colKey = "cust" + idx + "_color";
            // Note: enKey (checkbox) is NOT required - unchecked checkboxes don't send params
            bool hasAll = hasParam(nameKey) && hasParam(sKey) && hasParam(eKey) && hasParam(dirKey) && hasParam(dlyKey) && hasParam(effKey) && hasParam(colKey);
            if (!hasAll) continue;
            TmpCustom t;
            t.present = true;
            t.name = request->getParam(nameKey.c_str(), true)->value(); t.name.trim(); if (t.name.length()>32) t.name = t.name.substring(0,32);
            t.s = request->getParam(sKey.c_str(), true)->value().toInt();
            t.e = request->getParam(eKey.c_str(), true)->value().toInt();
            if (t.s < 0) t.s = 0; if (t.e >= NUM_LEDS) t.e = NUM_LEDS-1; if (t.s>t.e) t.e=t.s;
            t.dir = request->getParam(dirKey.c_str(), true)->value() == "1";
            t.en = request->hasParam(enKey.c_str(), true);
            t.dly = request->getParam(dlyKey.c_str(), true)->value().toInt(); if (t.dly<1) t.dly=1; if (t.dly>10000) t.dly=10000;
            t.eff = request->getParam(effKey.c_str(), true)->value().toInt(); if (t.eff<0||t.eff>2) t.eff=0;
            parseHexColor(colKey.c_str(), t.col);
            tmpCustoms[i] = t;
            addRange(t.name, t.s, t.e, t.en);
        }

        // Overlap detection among enabled ranges
        for (int i = 0; i < rc; ++i) {
            for (int j = i+1; j < rc; ++j) {
                int s1 = ranges[i].s, e1 = ranges[i].e;
                int s2 = ranges[j].s, e2 = ranges[j].e;
                if (min(e1,e2) >= max(s1,s2)) {
                    String msg = "Overlapping segments detected: '" + ranges[i].name + "' (" + String(s1) + "-" + String(e1) + ") and '" + ranges[j].name + "' (" + String(s2) + "-" + String(e2) + ").";
                    request->send(400, "text/plain", msg);
                    return;
                }
            }
        }

        // Save all to preferences
        prefs.putString("wind_name", wname);
        prefs.putString("solar_name", sname);
        prefs.putString("elec_prod_name", epname);
        prefs.putString("h2_trans_name", htname);
        prefs.putString("h2_stor1_name", hs1name);
        prefs.putString("h2_stor2_name", hs2name);
        prefs.putString("h2_cons_name", hcname);
        prefs.putString("fabr_name", fbname);
        prefs.putString("elec_tran_name", etname);
        prefs.putString("stor_tran_name", stname);
        prefs.putString("stor_pow_name", spname);
        prefs.putInt("wind_start", ws); prefs.putInt("wind_end", we);
        prefs.putInt("solar_start", ss); prefs.putInt("solar_end", se);
        prefs.putInt("elec_prod_s", eps); prefs.putInt("elec_prod_e", epe);
        prefs.putInt("h2_trans_s", hts); prefs.putInt("h2_trans_e", hte);
        prefs.putInt("h2_stor1_s", h1s); prefs.putInt("h2_stor1_e", h1e);
        prefs.putInt("h2_stor2_s", h2s); prefs.putInt("h2_stor2_e", h2e);
        prefs.putInt("h2_cons_s", hcs); prefs.putInt("h2_cons_e", hce);
        prefs.putInt("fabr_start", fs); prefs.putInt("fabr_end", fe);
        prefs.putInt("elec_tran_s", ets); prefs.putInt("elec_tran_e", ete);
        prefs.putInt("stor_tran_s", sts); prefs.putInt("stor_tran_e", ste);
        prefs.putInt("stor_pow_s", sps); prefs.putInt("stor_pow_e", spe);

    // Update runtime state
        state.windName = wname;
        state.solarName = sname;
        state.electricityProductionName = epname;
        state.hydrogenTransportName = htname;
        state.hydrogenStorage1Name = hs1name;
        state.hydrogenStorage2Name = hs2name;
        state.h2ConsumptionName = hcname;
        state.fabricationName = fbname;
        state.electricityTransportName = etname;
        state.storageTransportName = stname;
        state.storagePowerstationName = spname;
        state.windSegmentStart = ws; state.windSegmentEnd = we;
        state.solarSegmentStart = ss; state.solarSegmentEnd = se;
        state.electricityProductionSegmentStart = eps; state.electricityProductionSegmentEnd = epe;
        state.hydrogenTransportSegmentStart = hts; state.hydrogenTransportSegmentEnd = hte;
        state.hydrogenStorage1SegmentStart = h1s; state.hydrogenStorage1SegmentEnd = h1e;
        state.hydrogenStorage2SegmentStart = h2s; state.hydrogenStorage2SegmentEnd = h2e;
        state.hydrogenConsumptionSegmentStart = hcs; state.hydrogenConsumptionSegmentEnd = hce;
        state.fabricationSegmentStart = fs; state.fabricationSegmentEnd = fe;
        state.electricityTransportSegmentStart = ets; state.electricityTransportSegmentEnd = ete;
        state.storageTransportSegmentStart = sts; state.storageTransportSegmentEnd = ste;
        state.storagePowerstationSegmentStart = sps; state.storagePowerstationSegmentEnd = spe;

        // Save all to preferences (directions, enables, and delays too)
        prefs.putInt("wind_start", ws); prefs.putInt("wind_end", we); prefs.putBool("wind_dir", wdir); prefs.putBool("wind_en", wen); prefs.putInt("wind_delay", wdly);
        prefs.putInt("solar_start", ss); prefs.putInt("solar_end", se); prefs.putBool("solar_dir", sdir); prefs.putBool("solar_en", sen); prefs.putInt("solar_delay", sdly);
        prefs.putInt("elec_prod_s", eps); prefs.putInt("elec_prod_e", epe); prefs.putBool("elec_prod_dir", epdir); prefs.putBool("elec_prod_en", epen); prefs.putInt("elec_prod_delay", epdly);
        prefs.putInt("h2_trans_s", hts); prefs.putInt("h2_trans_e", hte); prefs.putBool("h2_trans_dir", htdir); prefs.putBool("h2_trans_en", hten); prefs.putInt("h2_trans_delay", htdly);
        prefs.putInt("h2_stor1_s", h1s); prefs.putInt("h2_stor1_e", h1e); prefs.putBool("h2_stor1_dir", h1dir); prefs.putInt("h2_stor1_delay", h1dly);
        prefs.putInt("h2_stor2_s", h2s); prefs.putInt("h2_stor2_e", h2e); prefs.putBool("h2_stor2_dir", h2dir); prefs.putBool("h2_stor_en", hsen); prefs.putInt("h2_stor2_delay", h2dly);
        prefs.putInt("h2_cons_s", hcs); prefs.putInt("h2_cons_e", hce); prefs.putBool("h2_cons_dir", hcdir); prefs.putBool("h2_cons_en", hcen); prefs.putInt("h2_cons_delay", hcdly);
        prefs.putInt("fabr_start", fs); prefs.putInt("fabr_end", fe); prefs.putBool("fabr_en", fben); prefs.putBool("fabr_dir", fbdir); prefs.putInt("fabr_delay", fbdly);
        prefs.putInt("elec_tran_s", ets); prefs.putInt("elec_tran_e", ete); prefs.putBool("elec_tran_dir", etdir); prefs.putBool("elec_tran_en", eten); prefs.putInt("elec_tran_delay", etdly);
        prefs.putInt("stor_tran_s", sts); prefs.putInt("stor_tran_e", ste); prefs.putBool("stor_tran_dir", stdir); prefs.putBool("stor_tran_en", sten); prefs.putInt("stor_tran_delay", stdly);
        prefs.putInt("stor_pow_s", sps); prefs.putInt("stor_pow_e", spe); prefs.putBool("stor_pow_dir", spdir); prefs.putBool("stor_pow_en", spen); prefs.putInt("stor_pow_delay", spdly);
        // Effect types
        prefs.putInt("wind_eff", weff);
        prefs.putInt("solar_eff", seff);
        prefs.putInt("elec_prod_eff", epeff);
        prefs.putInt("h2_trans_eff", hteff);
        prefs.putInt("h2_stor1_eff", h1eff);
        prefs.putInt("h2_stor2_eff", h2eff);
        prefs.putInt("h2_cons_eff", hceff);
        prefs.putInt("elec_tran_eff", eteff);
        prefs.putInt("stor_tran_eff", steff);
        prefs.putInt("stor_pow_eff", speff);
        prefs.putInt("fabr_eff", fbeff);
        prefs.putBool("electrolyser_en", elyen);
        // Save colors
        prefs.putUInt("wind_color", wcol);
        prefs.putUInt("solar_color", scol);
        prefs.putUInt("elec_prod_color", epcol);
        prefs.putUInt("h2_trans_color", htcol);
        prefs.putUInt("h2_stor1_color", hs1col);
        prefs.putUInt("h2_stor2_color", hs2col);
        prefs.putUInt("h2_cons_color", hccol);
        prefs.putUInt("fabr_color", fbcol);
        prefs.putUInt("elec_tran_color", etcol);
        prefs.putUInt("stor_tran_color", stcol);
        prefs.putUInt("stor_pow_color", spcol);

    programPrefs.putBool("auto_start", autoStart);
    programPrefs.putUInt("h2_trans_delay_s", static_cast<uint32_t>(h2delaySeconds));
    prefs.putUInt("h2_trans_delay_s", h2delaySeconds); // keep legacy namespace in sync for safety

        // Update directions in runtime state
        state.windDirForward = wdir;
        state.solarDirForward = sdir;
        state.electricityProductionDirForward = epdir;
        state.hydrogenTransportDirForward = htdir;
        state.hydrogenStorage1DirForward = h1dir;
        state.hydrogenStorage2DirForward = h2dir;
        state.h2ConsumptionDirForward = hcdir;
        state.electricityTransportDirForward = etdir;
        state.storageTransportDirForward = stdir;
        state.storagePowerstationDirForward = spdir;
        state.fabricationDirForward = fbdir;

        // Update enabled flags in runtime state
        state.windEnabled = wen;
        state.solarEnabled = sen;
        state.electricityProductionEnabled = epen;
        state.electrolyserEnabled = elyen;
        state.hydrogenTransportEnabled = hten;
        state.hydrogenStorageEnabled = hsen;
        state.h2ConsumptionEnabled = hcen;
        state.fabricationEnabled = fben;
        state.electricityTransportEnabled = eten;
        state.storageTransportEnabled = sten;
        state.storagePowerstationEnabled = spen;
        state.autoStartEnabled = autoStart;
        if (!autoStart) {
            state.autoStartTriggered = false;
            state.buttonDisabled = false;
        }

        // Update delays in runtime state
        state.windDelay = wdly;
        state.solarDelay = sdly;
        state.electricityProductionDelay = epdly;
        state.hydrogenTransportDelay = htdly;
        state.hydrogenStorage1Delay = h1dly;
        state.hydrogenStorage2Delay = h2dly;
        state.h2ConsumptionDelay = hcdly;
        state.electricityTransportDelay = etdly;
        state.storageTransportDelay = stdly;
        state.storagePowerstationDelay = spdly;
        state.fabricationDelay = fbdly;
    state.hydrogenTransportDelaySeconds = h2delaySeconds;

        // Update effect types in runtime
        state.windEffectType = weff;
        state.solarEffectType = seff;
        state.electricityProductionEffectType = epeff;
        state.hydrogenTransportEffectType = hteff;
        state.hydrogenStorage1EffectType = h1eff;
        state.hydrogenStorage2EffectType = h2eff;
        state.h2ConsumptionEffectType = hceff;
        state.electricityTransportEffectType = eteff;
        state.storageTransportEffectType = steff;
        state.storagePowerstationEffectType = speff;
        state.fabricationEffectType = fbeff;

        // Update colors in runtime
        auto unpackColorLocal = [](uint32_t v) -> CRGB { return CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); };
        state.windColor = unpackColorLocal(wcol);
        state.solarColor = unpackColorLocal(scol);
        state.electricityProductionColor = unpackColorLocal(epcol);
        state.hydrogenTransportColor = unpackColorLocal(htcol);
        state.hydrogenStorage1Color = unpackColorLocal(hs1col);
        state.hydrogenStorage2Color = unpackColorLocal(hs2col);
        state.h2ConsumptionColor = unpackColorLocal(hccol);
        state.fabricationColor = unpackColorLocal(fbcol);
        state.electricityTransportColor = unpackColorLocal(etcol);
        state.storageTransportColor = unpackColorLocal(stcol);
        state.storagePowerstationColor = unpackColorLocal(spcol);

        // Handle custom segments - use the tmpCustoms data already parsed for overlap validation
        for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
            if (!tmpCustoms[i].present) continue;  // Use tmpCustoms instead of checking state.custom[i].inUse
            auto &t = tmpCustoms[i];
            // Save prefs
            String p = String("cust") + String(i) + "_";
            prefs.putString((p+"name").c_str(), t.name);
            prefs.putInt((p+"s").c_str(), t.s);
            prefs.putInt((p+"e").c_str(), t.e);
            prefs.putBool((p+"dir").c_str(), t.dir);
            prefs.putBool((p+"en").c_str(), t.en);
            prefs.putInt((p+"delay").c_str(), t.dly);
            prefs.putInt((p+"eff").c_str(), t.eff);
            prefs.putUInt((p+"color").c_str(), t.col);
            // Update runtime
            auto &cs = state.custom[i];
            cs.name = t.name; 
            cs.start = t.s; 
            cs.end = t.e; 
            cs.dirForward = t.dir; 
            cs.enabled = t.en; 
            cs.delay = t.dly; 
            cs.effectType = t.eff; 
            cs.color = CRGB((t.col>>16)&0xFF,(t.col>>8)&0xFF,t.col&0xFF);
        }

        request->redirect("/");
    });

    // Test handler - starts test mode for a segment
    server.on("/test", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!request->hasParam("start", true) || !request->hasParam("end", true)) {
            request->send(400, "text/plain", "Missing parameters");
            return;
        }

        int start = request->getParam("start", true)->value().toInt();
        int end = request->getParam("end", true)->value().toInt();
        bool dir = true;
        if (request->hasParam("dir", true)) {
            String v = request->getParam("dir", true)->value();
            dir = (v == "1");
        }

        // Capture effect type, delay, and color if provided
        int effectType = 0;
        int delay = 500;
        CRGB color = CRGB::White;
        
        if (request->hasParam("eff", true)) {
            effectType = request->getParam("eff", true)->value().toInt();
            if (effectType < 0 || effectType > 2) effectType = 0;
        }
        
        if (request->hasParam("delay", true)) {
            delay = request->getParam("delay", true)->value().toInt();
            if (delay < 1) delay = 1;
            if (delay > 10000) delay = 10000;
        }
        
        if (request->hasParam("color", true)) {
            String colorStr = request->getParam("color", true)->value();
            if (colorStr.length() == 7 && colorStr[0] == '#') {
                String hex = colorStr.substring(1);
                uint32_t rgb = strtoul(hex.c_str(), nullptr, 16);
                color = CRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
            }
        }

        if (start < 0 || start >= NUM_LEDS || end < 0 || end >= NUM_LEDS || start > end) {
            request->send(400, "text/plain", "Invalid range");
            return;
        }

        // Enter test mode
        state.testMode = true;
        state.testSegmentStart = start;
        state.testSegmentEnd = end;
        // force runTestMode() to (re)initialize and clear by using an out-of-range sentinel
        state.testSegmentIndex = -1;
        state.testDirForward = dir;
        state.testEffectType = effectType;
        state.testColor = color;
        state.testDelay = delay;
        state.testPhase = 0; // Start with LED check phase
        state.testPhaseStartTime = millis();

        // Immediately clear all LEDs so test starts from a blank strip
        fill_solid(state.leds, NUM_LEDS, CRGB::Black);
        FastLED.show();

        request->redirect("/");
    });

    // Stop test handler
    server.on("/stoptest", HTTP_POST, [](AsyncWebServerRequest *request){
        state.testMode = false;
        // Clear LEDs on exit from test for a clean state
        fill_solid(state.leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        // Reset the program state to initial defaults
        resetAllVariables();
        request->redirect("/");
    });

    // Restart handler - posts here will restart the device after responding
    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        // Redirect to main page, then restart
        request->redirect("/");
        // create a small task to restart so we don't block the webserver thread
        xTaskCreate(restartTask, "restart", 2048, NULL, 1, NULL);
    });

    server.begin();
}
