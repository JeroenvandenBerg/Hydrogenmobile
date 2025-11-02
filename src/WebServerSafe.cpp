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

    // Load persisted wind segment start/end if present
    int persistedStart = prefs.getInt("wind_start", WIND_LED_START);
    int persistedEnd = prefs.getInt("wind_end", WIND_LED_END);
    // Sanitize
    if (persistedStart < 0) persistedStart = 0;
    if (persistedEnd >= NUM_LEDS) persistedEnd = NUM_LEDS - 1;
    if (persistedStart > persistedEnd) persistedEnd = persistedStart;
    state.windSegmentStart = persistedStart;
    state.windSegmentEnd = persistedEnd;

    // Serve root page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        char page[1024];
        snprintf(page, sizeof(page),
                 "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Wind Segment</title></head><body>"
                 "<h3>Wind LED Segment</h3>"
                 "<form method=\"POST\" action=\"/wind\">"
                 "Start: <input type=\"number\" name=\"start\" min=0 max=%d value=%d><br>"
                 "End: <input type=\"number\" name=\"end\" min=0 max=%d value=%d><br>"
                 "<button type=\"submit\">Save</button></form>"
                 "<hr>"
                 "<form method=\"POST\" action=\"/restart\" onsubmit=\"return confirm('Restart the device?')\">"
                 "<button type=\"submit\" style=\"background:#d9534f;color:white;padding:8px 12px;border:none;border-radius:4px;\">Restart ESP</button>"
                 "</form>"
                 "</body></html>", NUM_LEDS-1, state.windSegmentStart, NUM_LEDS-1, state.windSegmentEnd);
        request->send(200, "text/html", page);
    });

    // Handle update
    server.on("/wind", HTTP_POST, [](AsyncWebServerRequest *request){
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

        prefs.putInt("wind_start", start);
        prefs.putInt("wind_end", end);

        // Update runtime state as well
        state.windSegmentStart = start;
        state.windSegmentEnd = end;

        request->send(200, "text/plain", "Saved");
    });

    // Restart handler - posts here will restart the device after responding
    server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "Restarting");
        // create a small task to restart so we don't block the webserver thread
        xTaskCreate(restartTask, "restart", 2048, NULL, 1, NULL);
    });

    server.begin();
}
