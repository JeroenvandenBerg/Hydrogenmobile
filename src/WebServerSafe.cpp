#include "../include/WebServerSafe.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "../include/Config.h"
#include "../include/SystemState.h"
#include <Arduino.h>

// Use the global state defined in main.cpp
extern SystemState state;

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
    auto addSegment = [&](const char* name, const char* startName, const char* endName, int startVal, int endVal) {
            page += "<div class='segment'><b>" + String(name) + "</b><br>"
            "Start: <input id='" + String(startName) + "' type='number' name='" + String(startName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(startVal) + ">"
            " End: <input id='" + String(endName) + "' type='number' name='" + String(endName) + "' min=0 max=" + String(NUM_LEDS-1) + " value=" + String(endVal) + ">"
            "<button type='button' class='test' onclick=\"testSegment('" + String(startName) + "','" + String(endName) + "')\">Test</button>"
            "</div>";
        };
        
        addSegment("Wind", "wind_start", "wind_end", state.windSegmentStart, state.windSegmentEnd);
        addSegment("Solar", "solar_start", "solar_end", state.solarSegmentStart, state.solarSegmentEnd);
        addSegment("Electricity Production", "elec_prod_s", "elec_prod_e", state.electricityProductionSegmentStart, state.electricityProductionSegmentEnd);
        addSegment("Hydrogen Production", "h2_prod_s", "h2_prod_e", state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd);
        addSegment("Hydrogen Transport", "h2_trans_s", "h2_trans_e", state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
        addSegment("Hydrogen Storage 1", "h2_stor1_s", "h2_stor1_e", state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
        addSegment("Hydrogen Storage 2", "h2_stor2_s", "h2_stor2_e", state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
        addSegment("Hydrogen Consumption", "h2_cons_s", "h2_cons_e", state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
        addSegment("Fabrication", "fabr_start", "fabr_end", state.fabricationSegmentStart, state.fabricationSegmentEnd);
        addSegment("Electricity Transport", "elec_tran_s", "elec_tran_e", state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
        addSegment("Storage Transport", "stor_tran_s", "stor_tran_e", state.storageTransportSegmentStart, state.storageTransportSegmentEnd);
        addSegment("Storage Powerstation", "stor_pow_s", "stor_pow_e", state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);
        
        page += "<button type='submit'>Save All Settings</button></form><hr>"
            "<script>\n"
            "function testSegment(startName,endName){\n"
            "  const s=document.getElementById(startName).value;\n"
            "  const e=document.getElementById(endName).value;\n"
            "  const body=new URLSearchParams({start:s,end:e}).toString();\n"
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

        int ws, we, ss, se, eps, epe, hps, hpe, hts, hte, h1s, h1e, h2s, h2e, hcs, hce, fs, fe, ets, ete, sts, ste, sps, spe;
        
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
            !getSegment("stor_pow_s", "stor_pow_e", sps, spe)) {
            request->send(400, "text/plain", "Missing or invalid parameters");
            return;
        }

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

        if (start < 0 || start >= NUM_LEDS || end < 0 || end >= NUM_LEDS || start > end) {
            request->send(400, "text/plain", "Invalid range");
            return;
        }

        // Enter test mode
        state.testMode = true;
        state.testSegmentStart = start;
        state.testSegmentEnd = end;
        state.testSegmentIndex = start;

        request->redirect("/");
    });

    // Stop test handler
    server.on("/stoptest", HTTP_POST, [](AsyncWebServerRequest *request){
        state.testMode = false;
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
