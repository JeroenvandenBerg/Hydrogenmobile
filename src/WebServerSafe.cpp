#include "../include/WebServerSafe.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "../include/Config.h"
#include "../include/SystemState.h"
#include <Arduino.h>
#include <FastLED.h>

// Use the global state defined in main.cpp
extern SystemState state;

// Forward declaration from main.cpp to reset program state
void resetAllVariables();

static AsyncWebServer server(80);
static Preferences prefs;

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

    // Open preferences namespace
    prefs.begin("led-config", false);

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

    // Serve root page with all segments
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String page = "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LED Segments</title>"
            "<style>body{font-family:Arial,sans-serif;max-width:600px;margin:20px auto;padding:10px;}"
            "h3{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:5px;}"
            ".segment{background:#f9f9f9;padding:10px;margin:10px 0;border-radius:5px;}"
            "input{width:60px;padding:5px;margin:5px;}"
            "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;margin:5px;}"
            "button:hover{background:#45a049;}"
            ".restart{background:#d9534f;}"
            ".restart:hover{background:#c9302c;}"
            ".test{background:#5bc0de;padding:8px 12px;}"
            ".test:hover{background:#46b8da;}"
            ".stop{background:#f0ad4e;}"
            ".stop:hover{background:#ec971f;}"
            "</style></head><body>"
            "<h3>LED Segment Configuration</h3>";
        
        if (state.testMode) {
            page += "<div style='background:#fff3cd;padding:15px;border-radius:5px;margin:10px 0;border:2px solid #ffc107;'>"
                    "<b>TEST MODE ACTIVE</b><br>Testing segment " + String(state.testSegmentStart) + "-" + String(state.testSegmentEnd) + "<br>"
                    "<form method='POST' action='/stoptest' style='display:inline;'>"
                    "<button type='submit' class='stop'>Stop Test</button></form></div>";
        }
        
    page += "<form id='saveForm' method=\"POST\" action=\"/update\">";
        
        // Helper lambda to create segment row with test button
    auto addSegmentDir = [&](const char* name, const char* startName, const char* endName, const char* dirName, const char* enName, const char* delayName, int startVal, int endVal, bool dirVal, bool enVal, int delayVal) {
            page += "<div class='segment'><b>" + String(name) + "</b><br>"
            "Start: <input id='" + String(startName) + "' type='number' name='" + String(startName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(startVal) + ">"
            " End: <input id='" + String(endName) + "' type='number' name='" + String(endName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(endVal) + ">"
            " Direction: <select name='" + String(dirName) + "'>"
            "<option value='1'" + String(dirVal ? " selected" : "") + ">Forward</option>"
            "<option value='0'" + String(!dirVal ? " selected" : "") + ">Reverse</option>"
            "</select>"
            " Delay(ms): <input type='number' name='" + String(delayName) + "' min=1 max=10000 value=" + String(delayVal) + " style='width:70px;'>"
            " Enabled: <input type='checkbox' name='" + String(enName) + "' value='1'" + String(enVal ? " checked" : "") + ">"
            "<button type='button' class='test' onclick=\"testSegment('" + String(startName) + "','" + String(endName) + "','" + String(dirName) + "')\">Test</button>"
            "</div>";
        };
    auto addSegmentSimple = [&](const char* name, const char* startName, const char* endName, const char* enName, int startVal, int endVal, bool enVal) {
            page += "<div class='segment'><b>" + String(name) + "</b><br>"
            "Start: <input id='" + String(startName) + "' type='number' name='" + String(startName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(startVal) + ">"
            " End: <input id='" + String(endName) + "' type='number' name='" + String(endName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(endVal) + ">"
            " Enabled: <input type='checkbox' name='" + String(enName) + "' value='1'" + String(enVal ? " checked" : "") + ">"
            "<button type='button' class='test' onclick=\"testSegment('" + String(startName) + "','" + String(endName) + "','')\">Test</button>"
            "</div>";
        };
        
    addSegmentDir("Wind", "wind_start", "wind_end", "wind_dir", "wind_en", "wind_delay", state.windSegmentStart, state.windSegmentEnd, state.windDirForward, state.windEnabled, state.windDelay);
    addSegmentDir("Solar", "solar_start", "solar_end", "solar_dir", "solar_en", "solar_delay", state.solarSegmentStart, state.solarSegmentEnd, state.solarDirForward, state.solarEnabled, state.solarDelay);
    addSegmentDir("Electricity Production", "elec_prod_s", "elec_prod_e", "elec_prod_dir", "elec_prod_en", "elec_prod_delay", state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd, state.electricityProductionDirForward, state.electricityProductionEnabled, state.electricityProductionDelay);
    addSegmentSimple("Hydrogen Production", "h2_prod_s", "h2_prod_e", "h2_prod_en", state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd, state.hydrogenProductionEnabled);
    addSegmentDir("Hydrogen Transport", "h2_trans_s", "h2_trans_e", "h2_trans_dir", "h2_trans_en", "h2_trans_delay", state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd, state.hydrogenTransportDirForward, state.hydrogenTransportEnabled, state.hydrogenTransportDelay);
    addSegmentDir("Hydrogen Storage 1", "h2_stor1_s", "h2_stor1_e", "h2_stor1_dir", "h2_stor_en", "h2_stor1_delay", state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd, state.hydrogenStorage1DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage1Delay);
    addSegmentDir("Hydrogen Storage 2", "h2_stor2_s", "h2_stor2_e", "h2_stor2_dir", "h2_stor_en", "h2_stor2_delay", state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd, state.hydrogenStorage2DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage2Delay);
    addSegmentDir("Hydrogen Consumption", "h2_cons_s", "h2_cons_e", "h2_cons_dir", "h2_cons_en", "h2_cons_delay", state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd, state.h2ConsumptionDirForward, state.h2ConsumptionEnabled, state.h2ConsumptionDelay);
    addSegmentSimple("Fabrication", "fabr_start", "fabr_end", "fabr_en", state.fabricationSegmentStart, state.fabricationSegmentEnd, state.fabricationEnabled);
    addSegmentDir("Electricity Transport", "elec_tran_s", "elec_tran_e", "elec_tran_dir", "elec_tran_en", "elec_tran_delay", state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd, state.electricityTransportDirForward, state.electricityTransportEnabled, state.electricityTransportDelay);
    addSegmentDir("Storage Transport", "stor_tran_s", "stor_tran_e", "stor_tran_dir", "stor_tran_en", "stor_tran_delay", state.storageTransportSegmentStart, state.storageTransportSegmentEnd, state.storageTransportDirForward, state.storageTransportEnabled, state.storageTransportDelay);
    addSegmentDir("Storage Powerstation", "stor_pow_s", "stor_pow_e", "stor_pow_dir", "stor_pow_en", "stor_pow_delay", state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd, state.storagePowerstationDirForward, state.storagePowerstationEnabled, state.storagePowerstationDelay);

        // Non-segment control: Electrolyser enable
        page += String("<div class='segment'><b>Electrolyser</b><br> Enabled: ") +
                "<input type='checkbox' name='electrolyser_en' value='1'" + String(state.electrolyserEnabled ? " checked" : "") + ">" +
                "</div>";
        
        page += "<button type='submit'>Save All Settings</button></form><hr>"
            "<script>\n"
            "function testSegment(startName,endName,dirName){\n"
            "  const s=document.getElementById(startName).value;\n"
            "  const e=document.getElementById(endName).value;\n"
            "  let d='1';\n"
            "  if(dirName){ const sel=document.querySelector(\"select[name='\"+dirName+\"']\"); if(sel){ d=sel.value; } }\n"
            "  const body=new URLSearchParams({start:s,end:e,dir:d}).toString();\n"
            "  fetch('/test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Test request failed'));\n"
            "}\n"
            "</script>"
            "<form method='POST' action='/restart' onsubmit=\"return confirm('Restart the device?')\">"
            "<button type='submit' class='restart'>Restart ESP</button></form></body></html>";
        
        request->send(200, "text/html", page);
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

    int ws, we, ss, se, eps, epe, hps, hpe, hts, hte, h1s, h1e, h2s, h2e, hcs, hce, fs, fe, ets, ete, sts, ste, sps, spe;
    bool wdir, sdir, epdir, htdir, h1dir, h2dir, hcdir, etdir, stdir, spdir;
    int wdly, sdly, epdly, htdly, h1dly, h2dly, hcdly, etdly, stdly, spdly;
        
        if (!getSegment("wind_start", "wind_end", ws, we) ||
            !getSegment("solar_start", "solar_end", ss, se) ||
            !getSegment("elec_prod_s", "elec_prod_e", eps, epe) ||
            !getSegment("h2_prod_s", "h2_prod_e", hps, hpe) ||
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
            !getDelay("wind_delay", wdly) ||
            !getDelay("solar_delay", sdly) ||
            !getDelay("elec_prod_delay", epdly) ||
            !getDelay("h2_trans_delay", htdly) ||
            !getDelay("h2_stor1_delay", h1dly) ||
            !getDelay("h2_stor2_delay", h2dly) ||
            !getDelay("h2_cons_delay", hcdly) ||
            !getDelay("elec_tran_delay", etdly) ||
            !getDelay("stor_tran_delay", stdly) ||
            !getDelay("stor_pow_delay", spdly)) {
            request->send(400, "text/plain", "Missing or invalid parameters");
            return;
        }

        // Checkboxes (missing means false)
        bool wen  = getCheckbox("wind_en");
        bool sen  = getCheckbox("solar_en");
        bool epen = getCheckbox("elec_prod_en");
        bool hpen = getCheckbox("h2_prod_en");
        bool hten = getCheckbox("h2_trans_en");
        bool hsen = getCheckbox("h2_stor_en");
        bool hcen = getCheckbox("h2_cons_en");
        bool fben = getCheckbox("fabr_en");
        bool eten = getCheckbox("elec_tran_en");
        bool sten = getCheckbox("stor_tran_en");
        bool spen = getCheckbox("stor_pow_en");
        bool elyen = getCheckbox("electrolyser_en");

        // Save all to preferences
        prefs.putInt("wind_start", ws); prefs.putInt("wind_end", we);
        prefs.putInt("solar_start", ss); prefs.putInt("solar_end", se);
        prefs.putInt("elec_prod_s", eps); prefs.putInt("elec_prod_e", epe);
        prefs.putInt("h2_prod_s", hps); prefs.putInt("h2_prod_e", hpe);
        prefs.putInt("h2_trans_s", hts); prefs.putInt("h2_trans_e", hte);
        prefs.putInt("h2_stor1_s", h1s); prefs.putInt("h2_stor1_e", h1e);
        prefs.putInt("h2_stor2_s", h2s); prefs.putInt("h2_stor2_e", h2e);
    prefs.putInt("h2_cons_s", hcs); prefs.putInt("h2_cons_e", hce);
    prefs.putInt("fabr_start", fs); prefs.putInt("fabr_end", fe);
    prefs.putInt("elec_tran_s", ets); prefs.putInt("elec_tran_e", ete);
    prefs.putInt("stor_tran_s", sts); prefs.putInt("stor_tran_e", ste);
    prefs.putInt("stor_pow_s", sps); prefs.putInt("stor_pow_e", spe);

    // Update runtime state
        state.windSegmentStart = ws; state.windSegmentEnd = we;
        state.solarSegmentStart = ss; state.solarSegmentEnd = se;
        state.electricityProductionSegmentStart = eps; state.electricityProductionSegmentEnd = epe;
        state.hydrogenProductionSegmentStart = hps; state.hydrogenProductionSegmentEnd = hpe;
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
    prefs.putInt("h2_prod_s", hps); prefs.putInt("h2_prod_e", hpe); prefs.putBool("h2_prod_en", hpen);
    prefs.putInt("h2_trans_s", hts); prefs.putInt("h2_trans_e", hte); prefs.putBool("h2_trans_dir", htdir); prefs.putBool("h2_trans_en", hten); prefs.putInt("h2_trans_delay", htdly);
    prefs.putInt("h2_stor1_s", h1s); prefs.putInt("h2_stor1_e", h1e); prefs.putBool("h2_stor1_dir", h1dir); prefs.putInt("h2_stor1_delay", h1dly);
    prefs.putInt("h2_stor2_s", h2s); prefs.putInt("h2_stor2_e", h2e); prefs.putBool("h2_stor2_dir", h2dir); prefs.putBool("h2_stor_en", hsen); prefs.putInt("h2_stor2_delay", h2dly);
    prefs.putInt("h2_cons_s", hcs); prefs.putInt("h2_cons_e", hce); prefs.putBool("h2_cons_dir", hcdir); prefs.putBool("h2_cons_en", hcen); prefs.putInt("h2_cons_delay", hcdly);
    prefs.putInt("fabr_start", fs); prefs.putInt("fabr_end", fe); prefs.putBool("fabr_en", fben);
    prefs.putInt("elec_tran_s", ets); prefs.putInt("elec_tran_e", ete); prefs.putBool("elec_tran_dir", etdir); prefs.putBool("elec_tran_en", eten); prefs.putInt("elec_tran_delay", etdly);
    prefs.putInt("stor_tran_s", sts); prefs.putInt("stor_tran_e", ste); prefs.putBool("stor_tran_dir", stdir); prefs.putBool("stor_tran_en", sten); prefs.putInt("stor_tran_delay", stdly);
    prefs.putInt("stor_pow_s", sps); prefs.putInt("stor_pow_e", spe); prefs.putBool("stor_pow_dir", spdir); prefs.putBool("stor_pow_en", spen); prefs.putInt("stor_pow_delay", spdly);
    prefs.putBool("electrolyser_en", elyen);

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

    // Update enabled flags in runtime state
    state.windEnabled = wen;
    state.solarEnabled = sen;
    state.electricityProductionEnabled = epen;
    state.electrolyserEnabled = elyen;
    state.hydrogenProductionEnabled = hpen;
    state.hydrogenTransportEnabled = hten;
    state.hydrogenStorageEnabled = hsen;
    state.h2ConsumptionEnabled = hcen;
    state.fabricationEnabled = fben;
    state.electricityTransportEnabled = eten;
    state.storageTransportEnabled = sten;
    state.storagePowerstationEnabled = spen;

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
