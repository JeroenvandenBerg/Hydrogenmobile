#include "../include/WebServerSafe.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "../include/Config.h"
#include "../include/SystemState.h"
#include "../include/Hardware.h"
#include <Arduino.h>
#include <FastLED.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "../include/logo_data_uri.h"
#include "../include/effects/EffectUtils.h"

// Use the global state defined in main.cpp
extern SystemState state;
extern Timers timers;

static AsyncWebServer server(80);
static Preferences prefs;
static Preferences programPrefs;
static constexpr const char* LED_CONFIG_NAMESPACE = "led-config";
static constexpr const char* PROGRAM_NAMESPACE = "program";
static constexpr const char* H2_DELAY_KEY = "h2_td_s";
static constexpr const char* H2_DELAY_KEY_LEGACY = "h2_trans_delay_s";
static constexpr const char* WIND_STOP_KEY = "w_stop_s";
static constexpr const char* STORAGE_RUN_KEY = "st_run_s";
static constexpr const char* RESTART_DELAY_KEY = "rst_dly_s";
static constexpr const char* BRIGHTNESS_DIVISOR_KEY = "bri_div";
static constexpr const char* ELECTROLYSER_RELAY_PIN_KEY = "ely_rel_pin"; // NVS keys must be <= 15 chars

static Preferences* resolveGlobalPrefsHandle(const char* ns) {
    if (ns == nullptr) return nullptr;
    if (strcmp(ns, LED_CONFIG_NAMESPACE) == 0) return &prefs;
    if (strcmp(ns, PROGRAM_NAMESPACE) == 0) return &programPrefs;
    return nullptr;
}

static bool reopenGlobalPreferences() {
    prefs.end();
    programPrefs.end();
    bool prefsOk = prefs.begin(LED_CONFIG_NAMESPACE, false);
    bool programPrefsOk = programPrefs.begin(PROGRAM_NAMESPACE, false);
    Serial.printf("Preferences reopen after NVS recovery: led-config=%d, program=%d\n", prefsOk ? 1 : 0, programPrefsOk ? 1 : 0);
    return prefsOk && programPrefsOk;
}

static bool probeHandleWritable(Preferences& p, const char* probeKey) {
    if (probeKey == nullptr || probeKey[0] == '\0') {
        return false;
    }
    uint8_t marker = static_cast<uint8_t>(millis() & 0xFF);
    size_t wrote = p.putUChar(probeKey, marker);
    uint8_t readBack = p.getUChar(probeKey, static_cast<uint8_t>(marker ^ 0xFF));
    return wrote == sizeof(uint8_t) && readBack == marker;
}

static bool reinitializeNvsAtBoot(bool erasePartition) {
    prefs.end();
    programPrefs.end();
    nvs_flash_deinit();

    if (erasePartition) {
        esp_err_t eraseErr = nvs_flash_erase();
        if (eraseErr != ESP_OK) {
            Serial.printf("NVS erase failed: %d\n", static_cast<int>(eraseErr));
            return false;
        }
    }

    esp_err_t initErr = nvs_flash_init();
    if (initErr == ESP_ERR_NVS_NO_FREE_PAGES || initErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) return false;
        initErr = nvs_flash_init();
    }
    if (initErr != ESP_OK) {
        Serial.printf("NVS reinit failed: %d\n", static_cast<int>(initErr));
        return false;
    }

    return reopenGlobalPreferences();
}

static bool _nvsRecoveryAttempted = false;

static void resetNvsRecoveryFlag() {
    _nvsRecoveryAttempted = false;
}

static bool tryFullNvsRecoveryOnce() {
    if (_nvsRecoveryAttempted) {
        return false;
    }
    _nvsRecoveryAttempted = true;

    Serial.println("Attempting NVS recovery (reinit only, no erase)...");

    prefs.end();
    programPrefs.end();
    nvs_flash_deinit();
    esp_err_t initErr = nvs_flash_init();
    if (initErr != ESP_OK) {
        Serial.printf("NVS reinit failed: %d\n", static_cast<int>(initErr));
        nvs_flash_init(); // best effort
        reopenGlobalPreferences();
        return false;
    }
    if (!reopenGlobalPreferences()) {
        Serial.println("NVS recovery: reopen failed");
        return false;
    }

    Serial.println("NVS recovery (reinit) succeeded; retrying write");
    return true;
}

static bool forceEraseNvsKey(const char* ns, const char* key) {
    if (ns == nullptr || key == nullptr || key[0] == '\0') {
        return false;
    }

    nvs_handle_t handle;
    esp_err_t openErr = nvs_open(ns, NVS_READWRITE, &handle);
    if (openErr != ESP_OK) {
        Serial.printf("forceErase: nvs_open failed: %d\n", (int)openErr);
        return false;
    }

    esp_err_t eraseErr = nvs_erase_key(handle, key);
    Serial.printf("forceErase: nvs_erase_key(%s, %s) = %d\n", ns, key, (int)eraseErr);
    
    // If erase is INVALID_STATE (4354), we need full NVS recovery - key is permanently broken
    if (eraseErr == ESP_ERR_NVS_INVALID_STATE) {
        Serial.println("forceErase: Key is INVALID_STATE - cannot erase, full NVS recovery needed");
        nvs_close(handle);
        return false;  // Signal that we need tryFullNvsRecoveryOnce()
    }
    
    if (eraseErr != ESP_OK && eraseErr != ESP_ERR_NVS_NOT_FOUND) {
        Serial.printf("forceErase: nvs_erase_key failed with unexpected error: %d\n", (int)eraseErr);
        nvs_close(handle);
        return false;
    }

    esp_err_t commitErr = nvs_commit(handle);
    Serial.printf("forceErase: nvs_commit = %d\n", (int)commitErr);
    nvs_close(handle);
    if (commitErr != ESP_OK) {
        Serial.printf("forceErase: nvs_commit failed: %d\n", (int)commitErr);
        return false;
    }

    reopenGlobalPreferences();
    Serial.println("forceErase: key erased and global prefs reopened");
    return true;
}

static bool putUIntRaw(const char* ns, const char* key, uint32_t value) {
    if (Preferences* gp = resolveGlobalPrefsHandle(ns)) {
        if (gp->isKey(key) && gp->getUInt(key, value ^ 0xA5A5A5A5UL) == value) {
            return true;
        }
        size_t wrote = gp->putUInt(key, value);
        if (wrote == sizeof(uint32_t) && gp->getUInt(key, value ^ 0xA5A5A5A5UL) == value) {
            return true;
        }
        if (gp->isKey(key)) {
            gp->remove(key);
            wrote = gp->putUInt(key, value);
            if (wrote == sizeof(uint32_t) && gp->getUInt(key, value ^ 0xA5A5A5A5UL) == value) {
                return true;
            }
        }
        // Fall back to isolated handle path below.
    }

    Preferences p;
    if (!p.begin(ns, false)) {
        p.end();
        return false;
    }
    if (p.isKey(key) && p.getUInt(key, value ^ 0xA5A5A5A5UL) == value) {
        p.end();
        return true;
    }
    size_t wrote = p.putUInt(key, value);
    uint32_t readBack = p.getUInt(key, value ^ 0xA5A5A5A5UL);
    if (wrote != sizeof(uint32_t) || readBack != value) {
        if (p.isKey(key)) {
            p.remove(key);
            wrote = p.putUInt(key, value);
            readBack = p.getUInt(key, value ^ 0xA5A5A5A5UL);
        }
    }
    p.end();
    return wrote == sizeof(uint32_t) && readBack == value;
}

static bool putIntRaw(const char* ns, const char* key, int32_t value) {
    if (Preferences* gp = resolveGlobalPrefsHandle(ns)) {
        if (gp->isKey(key) && gp->getInt(key, value ^ 0x5A5A5A5A) == value) {
            return true;
        }
        size_t wrote = gp->putInt(key, value);
        if (wrote == sizeof(int32_t) && gp->getInt(key, value ^ 0x5A5A5A5A) == value) {
            return true;
        }
        if (gp->isKey(key)) {
            gp->remove(key);
            wrote = gp->putInt(key, value);
            if (wrote == sizeof(int32_t) && gp->getInt(key, value ^ 0x5A5A5A5A) == value) {
                return true;
            }
        }
        // Fall back to isolated handle path below.
    }

    Preferences p;
    if (!p.begin(ns, false)) {
        p.end();
        return false;
    }
    if (p.isKey(key) && p.getInt(key, value ^ 0x5A5A5A5A) == value) {
        p.end();
        return true;
    }
    size_t wrote = p.putInt(key, value);
    int32_t readBack = p.getInt(key, value ^ 0x5A5A5A5A);
    if (wrote != sizeof(int32_t) || readBack != value) {
        if (p.isKey(key)) {
            p.remove(key);
            wrote = p.putInt(key, value);
            readBack = p.getInt(key, value ^ 0x5A5A5A5A);
        }
    }
    p.end();
    return wrote == sizeof(int32_t) && readBack == value;
}

static bool putStringRaw(const char* ns, const char* key, const String& value) {
    if (Preferences* gp = resolveGlobalPrefsHandle(ns)) {
        if (gp->isKey(key) && gp->getString(key, "") == value) {
            return true;
        }
        size_t wrote = gp->putString(key, value);
        if (wrote == value.length() && gp->getString(key, "") == value) {
            return true;
        }
        if (gp->isKey(key)) {
            gp->remove(key);
            wrote = gp->putString(key, value);
            if (wrote == value.length() && gp->getString(key, "") == value) {
                return true;
            }
        }
        // Fall back to isolated handle path below.
    }

    Preferences p;
    if (!p.begin(ns, false)) {
        p.end();
        return false;
    }
    if (p.isKey(key) && p.getString(key, "") == value) {
        p.end();
        return true;
    }
    size_t wrote = p.putString(key, value);
    String readBack = p.getString(key, "");
    if (wrote != value.length() || readBack != value) {
        if (p.isKey(key)) {
            p.remove(key);
            wrote = p.putString(key, value);
            readBack = p.getString(key, "");
        }
    }
    p.end();
    return wrote == value.length() && readBack == value;
}

static bool putBoolRaw(const char* ns, const char* key, bool value) {
    if (Preferences* gp = resolveGlobalPrefsHandle(ns)) {
        if (gp->isKey(key) && gp->getBool(key, !value) == value) {
            return true;
        }
        size_t wrote = gp->putBool(key, value);
        if (wrote == sizeof(uint8_t) && gp->getBool(key, !value) == value) {
            return true;
        }
        if (gp->isKey(key)) {
            gp->remove(key);
            wrote = gp->putBool(key, value);
            if (wrote == sizeof(uint8_t) && gp->getBool(key, !value) == value) {
                return true;
            }
        }
        // Fall back to isolated handle path below.
    }

    Preferences p;
    if (!p.begin(ns, false)) {
        p.end();
        return false;
    }
    if (p.isKey(key) && p.getBool(key, !value) == value) {
        p.end();
        return true;
    }
    size_t wrote = p.putBool(key, value);
    bool readBack = p.getBool(key, !value);
    if (wrote != sizeof(uint8_t) || readBack != value) {
        if (p.isKey(key)) {
            p.remove(key);
            wrote = p.putBool(key, value);
            readBack = p.getBool(key, !value);
        }
    }
    p.end();
    return wrote == sizeof(uint8_t) && readBack == value;
}

static bool putUIntRobust(const char* ns, const char* key, uint32_t value) {
    if (putUIntRaw(ns, key, value)) return true;

    Serial.printf("putUInt first attempt failed: ns=%s key=%s\n", ns, key);
    if (reopenGlobalPreferences() && putUIntRaw(ns, key, value)) return true;

    if (forceEraseNvsKey(ns, key) && putUIntRaw(ns, key, value)) return true;

    // Try isolated handle before full recovery
    Serial.printf("putUInt: final retry after failed erase - opening isolated handle for %s/%s\n", ns, key);
    Preferences p;
    if (!p.begin(ns, false)) {
        p.end();
    } else {
        if (p.isKey(key)) {
            p.remove(key);
        }
        size_t wrote = p.putUInt(key, value);
        uint32_t readBack = p.getUInt(key, value ^ 0xA5A5A5A5UL);
        p.end();
        if (wrote == sizeof(uint32_t) && readBack == value) {
            Serial.printf("putUInt: isolated handle retry succeeded for %s\n", key);
            reopenGlobalPreferences();
            return true;
        }
    }

    if (tryFullNvsRecoveryOnce() && putUIntRaw(ns, key, value)) return true;

    Serial.printf("putUInt failed after all retries: ns=%s key=%s\n", ns, key);
    return false;
}

static bool putIntRobust(const char* ns, const char* key, int32_t value) {
    if (putIntRaw(ns, key, value)) return true;

    Serial.printf("putInt first attempt failed: ns=%s key=%s\n", ns, key);
    if (reopenGlobalPreferences() && putIntRaw(ns, key, value)) return true;

    if (forceEraseNvsKey(ns, key) && putIntRaw(ns, key, value)) return true;

    // If forceErase returned false because of INVALID_STATE, that means full recovery is needed
    // But recovery with nvs_flash_init() will wipe all other settings
    // Try one more time with a fresh isolated handle after erase attempt
    Serial.printf("putInt: final retry after failed erase - opening isolated handle for %s/%s\n", ns, key);
    Preferences p;
    if (!p.begin(ns, false)) {
        p.end();
    } else {
        if (p.isKey(key)) {
            p.remove(key);  // Try to forcefully remove the key one more time
        }
        size_t wrote = p.putInt(key, value);
        int32_t readBack = p.getInt(key, value ^ 0x5A5A5A5A);
        p.end();
        if (wrote == sizeof(int32_t) && readBack == value) {
            Serial.printf("putInt: isolated handle retry succeeded for %s\n", key);
            reopenGlobalPreferences();  // Reopen global after isolated write
            return true;
        }
    }

    // Only as last resort, do full recovery WITH erase to clean up INVALID_STATE keys
    if (tryFullNvsRecoveryOnce() && putIntRaw(ns, key, value)) return true;

    Serial.printf("putInt failed after all retries: ns=%s key=%s\n", ns, key);
    return false;
}

static bool putStringRobust(const char* ns, const char* key, const String& value) {
    if (putStringRaw(ns, key, value)) return true;

    Serial.printf("putString first attempt failed: ns=%s key=%s\n", ns, key);
    if (reopenGlobalPreferences() && putStringRaw(ns, key, value)) return true;

    if (forceEraseNvsKey(ns, key) && putStringRaw(ns, key, value)) return true;

    if (tryFullNvsRecoveryOnce() && putStringRaw(ns, key, value)) return true;

    Serial.printf("putString failed after reopen retry: ns=%s key=%s\n", ns, key);
    return false;
}

static bool putBoolRobust(const char* ns, const char* key, bool value) {
    if (putBoolRaw(ns, key, value)) return true;

    Serial.printf("putBool first attempt failed: ns=%s key=%s\n", ns, key);
    if (reopenGlobalPreferences() && putBoolRaw(ns, key, value)) return true;

    if (forceEraseNvsKey(ns, key) && putBoolRaw(ns, key, value)) return true;

    // Try isolated handle before full recovery
    Serial.printf("putBool: final retry after failed erase - opening isolated handle for %s/%s\n", ns, key);
    Preferences p;
    if (!p.begin(ns, false)) {
        p.end();
    } else {
        if (p.isKey(key)) {
            p.remove(key);
        }
        size_t wrote = p.putBool(key, value);
        bool readBack = p.getBool(key, !value);
        p.end();
        if (wrote == sizeof(uint8_t) && readBack == value) {
            Serial.printf("putBool: isolated handle retry succeeded for %s\n", key);
            reopenGlobalPreferences();
            return true;
        }
    }

    if (tryFullNvsRecoveryOnce() && putBoolRaw(ns, key, value)) return true;

    Serial.printf("putBool failed after all retries: ns=%s key=%s\n", ns, key);
    return false;
}

static bool isSafeOutputGpio(uint8_t pin) {
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

// Task to restart the ESP after sending the response
static void restartTask(void *pvParameters) {
    // small delay to allow the HTTP response to be sent
    vTaskDelay(200 / portTICK_PERIOD_MS);
    ESP.restart();
    vTaskDelete(NULL);
}

void initWebServerSafe() {
    // Start AP with explicit AP mode and retries so new boards reliably expose the SSID.
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    delay(50);
    WiFi.setTxPower(WIFI_POWER_2dBm);

    // Allow multiple concurrent browser connections (HTML + JS fetches + retries).
    bool apStarted = WiFi.softAP("HydrogenDemo", "12345678", 1, false, 4);
    if (!apStarted) {
        Serial.println("SoftAP start failed, retrying with default channel...");
        apStarted = WiFi.softAP("HydrogenDemo", "12345678");
    }
    if (!apStarted) {
        Serial.println("SoftAP start still failed, retrying open AP fallback...");
        apStarted = WiFi.softAP("HydrogenDemo");
    }

    Serial.print("SoftAP status: ");
    Serial.println(apStarted ? "STARTED" : "FAILED");
    Serial.print("SoftAP SSID: ");
    Serial.println(WiFi.softAPSSID());
    Serial.print("Web UI AP IP: ");
    Serial.println(WiFi.softAPIP());

    // Ensure NVS is initialized and recover automatically from stale/full state.
    esp_err_t nvsInitErr = nvs_flash_init();
    if (nvsInitErr == ESP_ERR_NVS_INVALID_STATE) {
        Serial.println("NVS init returned INVALID_STATE, trying deinit/init...");
        nvs_flash_deinit();
        nvsInitErr = nvs_flash_init();
    }
    if (nvsInitErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsInitErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("NVS invalid/full, erasing NVS partition...");
        nvs_flash_erase();
        nvsInitErr = nvs_flash_init();
    }
    if (nvsInitErr != ESP_OK) {
        Serial.printf("NVS init failed: %d\n", static_cast<int>(nvsInitErr));
    }

    // Open preferences namespaces and log if any namespace failed to open.
    bool prefsOk = prefs.begin(LED_CONFIG_NAMESPACE, false);
    bool programPrefsOk = programPrefs.begin(PROGRAM_NAMESPACE, false);
    if (!prefsOk || !programPrefsOk) {
        Serial.printf("Preferences open failed: led-config=%d, program=%d\n", prefsOk ? 1 : 0, programPrefsOk ? 1 : 0);
        // Retry once in case namespace handles are stale after NVS recovery.
        prefs.end();
        programPrefs.end();
        prefsOk = prefs.begin(LED_CONFIG_NAMESPACE, false);
        programPrefsOk = programPrefs.begin(PROGRAM_NAMESPACE, false);
        Serial.printf("Preferences reopen: led-config=%d, program=%d\n", prefsOk ? 1 : 0, programPrefsOk ? 1 : 0);
    }

    bool nvsWritable = prefsOk && programPrefsOk;
    if (nvsWritable) {
        bool probeOk = probeHandleWritable(prefs, "wr_led") && probeHandleWritable(programPrefs, "wr_prog");
        if (!probeOk) {
            // Probe is advisory only. Avoid destructive erase at boot; recover lazily on first real write failure.
            Serial.println("NVS write probe failed at boot; skipping boot-time writes and deferring recovery to save operations");
            nvsWritable = false;
        }
    }

    uint32_t storedLedCount = programPrefs.getUInt("total_leds", state.totalLeds);
    if (storedLedCount < 1) storedLedCount = 1;
    if (storedLedCount > NUM_LEDS) storedLedCount = NUM_LEDS;
    state.totalLeds = static_cast<uint16_t>(storedLedCount);

    auto loadPin = [&](const char* key, uint8_t defVal) {
        int v = prefs.getInt(key, defVal);
        if (v < 0 || v > 39) v = defVal;
        return static_cast<uint8_t>(v);
    };
    auto loadOutputPin = [&](const char* key, uint8_t defVal) {
        uint8_t v = loadPin(key, defVal);
        if (!isSafeOutputGpio(v)) return defVal;
        return v;
    };

    // Migrate program-level settings from legacy keys if needed
    if (!programPrefs.isKey("auto_start") && prefs.isKey("auto_start_enabled")) {
        bool legacyAuto = prefs.getBool("auto_start_enabled", false);
        if (nvsWritable) {
            putBoolRobust(PROGRAM_NAMESPACE, "auto_start", legacyAuto);
        }
    }
    if (!programPrefs.isKey(H2_DELAY_KEY) && prefs.isKey(H2_DELAY_KEY_LEGACY)) {
        uint32_t legacyDelay = prefs.getUInt(H2_DELAY_KEY_LEGACY, state.hydrogenTransportDelaySeconds);
        if (legacyDelay > 600) legacyDelay = 600;
        if (nvsWritable && !putUIntRobust(PROGRAM_NAMESPACE, H2_DELAY_KEY, legacyDelay)) {
            Serial.println("Failed to migrate legacy hydrogen transport delay");
        }
    }
    if (!programPrefs.isKey(WIND_STOP_KEY) && prefs.isKey("wind_time_s")) {
        uint32_t legacyWindStop = prefs.getUInt("wind_time_s", state.windStopSeconds);
        if (legacyWindStop > 600) legacyWindStop = 600;
        if (nvsWritable && !putUIntRobust(PROGRAM_NAMESPACE, WIND_STOP_KEY, legacyWindStop)) {
            Serial.println("Failed to migrate legacy wind stop delay");
        }
    }

    // Load all persisted segments with defaults from Config.h
    auto loadSegment = [&](const char* startKey, const char* endKey, int defStart, int defEnd, int &outStart, int &outEnd) {
        int maxIndex = state.totalLeds > 0 ? state.totalLeds - 1 : 0;
        if (defStart < 0) defStart = 0;
        if (defStart > maxIndex) defStart = maxIndex;
        if (defEnd < 0) defEnd = 0;
        if (defEnd > maxIndex) defEnd = maxIndex;
        int s = prefs.getInt(startKey, defStart);
        int e = prefs.getInt(endKey, defEnd);
        if (s < 0) s = 0;
        if (s > maxIndex) s = maxIndex;
        if (e < 0) e = 0;
        if (e > maxIndex) e = maxIndex;
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
        if (!prefs.isKey(key)) return String(defVal);
        return prefs.getString(key, defVal);
    };
    auto loadMigratedName = [&](const char* key, const char* legacyVal, const char* newVal) -> String {
        String value = loadName(key, newVal);
        if (value == legacyVal) {
            return String(newVal);
        }
        return value;
    };
    state.windName = loadName("wind_name", "Wind");
    state.solarName = loadName("solar_name", "Solar");
    state.hydrogenProductionName = loadName("h2_prod_name", "Hydrogen Production");
    state.hydrogenTransportName = loadName("h2_trans_name", "Hydrogen Transport");
    state.hydrogenStorage1Name = loadMigratedName("h2_stor1_name", "Hydrogen Storage 1", "Hydrogen Storage In");
    state.hydrogenStorage2Name = loadMigratedName("h2_stor2_name", "Hydrogen Storage 2", "Hydrogen Storage Out");
    state.h2ConsumptionName = loadMigratedName("h2_cons_name", "Hydrogen Consumption", "Fabrication Direct");
    state.fabricationName = loadMigratedName("fabr_name", "Fabrication", "Fabrication Fire");
    state.electricityTransportName = loadMigratedName("elec_tran_name", "Electricity Transport", "Fabrication Storage");
    state.storagePowerstationName = loadName("stor_pow_name", "Storage Powerstation");

    loadSegment("wind_start", "wind_end", WIND_LED_START, WIND_LED_END, state.windSegmentStart, state.windSegmentEnd);
    loadSegment("solar_start", "solar_end", SOLAR_LED_START, SOLAR_LED_END, state.solarSegmentStart, state.solarSegmentEnd);
    loadSegment("h2_prod_s", "h2_prod_e", HYDROGEN_PRODUCTION_LED_START, HYDROGEN_PRODUCTION_LED_END, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd);
    if (state.hydrogenProductionSegmentStart == 17 && state.hydrogenProductionSegmentEnd == 22) {
        state.hydrogenProductionSegmentStart = HYDROGEN_PRODUCTION_LED_START;
        state.hydrogenProductionSegmentEnd = HYDROGEN_PRODUCTION_LED_END;
    }
    loadSegment("h2_trans_s", "h2_trans_e", HYDROGEN_TRANSPORT_LED_START, HYDROGEN_TRANSPORT_LED_END, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd);
    loadSegment("h2_stor1_s", "h2_stor1_e", HYDROGEN_STORAGE1_LED_START, HYDROGEN_STORAGE1_LED_END, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd);
    loadSegment("h2_stor2_s", "h2_stor2_e", HYDROGEN_STORAGE2_LED_START, HYDROGEN_STORAGE2_LED_END, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd);
    loadSegment("h2_cons_s", "h2_cons_e", HYDROGEN_CONSUMPTION_LED_START, HYDROGEN_CONSUMPTION_LED_END, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd);
    loadSegment("fabr_start", "fabr_end", FABRICATION_LED_START, FABRICATION_LED_END, state.fabricationSegmentStart, state.fabricationSegmentEnd);
    loadSegment("elec_tran_s", "elec_tran_e", ELECTRICITY_TRANSPORT_LED_START, ELECTRICITY_TRANSPORT_LED_END, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd);
    loadSegment("stor_pow_s", "stor_pow_e", STORAGE_POWERSTATION_LED_START, STORAGE_POWERSTATION_LED_END, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd);

    // Load directions with defaults that match historical behavior
    state.windDirForward = loadDir("wind_dir", true);
    state.solarDirForward = loadDir("solar_dir", false);
    state.hydrogenTransportDirForward = loadDir("h2_trans_dir", true);
    state.hydrogenStorage1DirForward = loadDir("h2_stor1_dir", true);
    state.hydrogenStorage2DirForward = loadDir("h2_stor2_dir", true);
    state.h2ConsumptionDirForward = loadDir("h2_cons_dir", true);
    state.electricityTransportDirForward = loadDir("elec_tran_dir", true);
    state.storagePowerstationDirForward = loadDir("stor_pow_dir", true);

    // Load enabled flags
    state.windEnabled = loadEn("wind_en", true);
    state.solarEnabled = loadEn("solar_en", true);
    state.electrolyserEnabled = loadEn("electrolyser_en", true);
    state.hydrogenProductionEnabled = loadEn("h2_prod_en", true);
    state.hydrogenTransportEnabled = loadEn("h2_trans_en", true);
    state.hydrogenStorageEnabled = loadEn("h2_stor_en", true);
    state.h2ConsumptionEnabled = loadEn("h2_cons_en", true);
    state.fabricationEnabled = loadEn("fabr_en", true);
    state.electricityTransportEnabled = loadEn("elec_tran_en", true);
    state.storagePowerstationEnabled = loadEn("stor_pow_en", true);

    // Legacy segments not used in current flow: keep disabled.
    state.electricityProductionEnabled = false;
    state.storageTransportEnabled = false;

    // Load delays
    state.brightnessDivisor = prefs.getInt(BRIGHTNESS_DIVISOR_KEY, 1);
    if (state.brightnessDivisor < 1) state.brightnessDivisor = 1;
    if (state.brightnessDivisor > 10) state.brightnessDivisor = 10;

    state.windDelay = loadDelay("wind_delay", LED_DELAY);
    state.solarDelay = loadDelay("solar_delay", LED_DELAY);
    state.hydrogenTransportDelay = loadDelay("h2_trans_delay", LED_DELAY);
    state.hydrogenStorage1Delay = loadDelay("h2_stor1_delay", LED_DELAY);
    state.hydrogenStorage2Delay = loadDelay("h2_stor2_delay", LED_DELAY);
    state.h2ConsumptionDelay = loadDelay("h2_cons_delay", LED_DELAY);
    state.electricityTransportDelay = loadDelay("elec_tran_delay", LED_DELAY);
    state.storagePowerstationDelay = loadDelay("stor_pow_delay", LED_DELAY2);

    // Load effect types
    // Load all effect types as 3-option: 0=Running, 1=Fire, 2=Fade
    state.windEffectType = loadEffect3("wind_eff", 0);
    state.solarEffectType = loadEffect3("solar_eff", 0);
    state.hydrogenTransportEffectType = loadEffect3("h2_trans_eff", 0);
    state.hydrogenStorage1EffectType = loadEffect3("h2_stor1_eff", 0);
    state.hydrogenStorage2EffectType = loadEffect3("h2_stor2_eff", 0);
    state.h2ConsumptionEffectType = loadEffect3("h2_cons_eff", 0);
    state.electricityTransportEffectType = loadEffect3("elec_tran_eff", 0);
    state.storagePowerstationEffectType = loadEffect3("stor_pow_eff", 0);
    state.hydrogenProductionEffectType = loadEffect3("h2_prod_eff", 0);
    state.fabricationEffectType = loadEffect3("fabr_eff", 0);
    
    Serial.printf("Loaded effects after boot: wind=%d solar=%d h2trans=%d h2stor1=%d h2stor2=%d h2cons=%d elec=%d storagepow=%d h2prod=%d fabr=%d\n",
        state.windEffectType, state.solarEffectType, state.hydrogenTransportEffectType, state.hydrogenStorage1EffectType,
        state.hydrogenStorage2EffectType, state.h2ConsumptionEffectType, state.electricityTransportEffectType,
        state.storagePowerstationEffectType, state.hydrogenProductionEffectType, state.fabricationEffectType);

    state.autoStartEnabled = programPrefs.getBool("auto_start", false);
    uint32_t transportDelayRaw = programPrefs.getUInt(H2_DELAY_KEY, UINT32_MAX);
    if (transportDelayRaw == UINT32_MAX) {
        transportDelayRaw = prefs.getUInt(H2_DELAY_KEY, UINT32_MAX);
    }
    if (transportDelayRaw == UINT32_MAX) {
        // Read legacy key only as fallback; never write it because it exceeds NVS key length.
        transportDelayRaw = prefs.getUInt(H2_DELAY_KEY_LEGACY, state.hydrogenTransportDelaySeconds);
    }
    uint16_t transportDelaySec = static_cast<uint16_t>(transportDelayRaw);
    if (nvsWritable && !programPrefs.isKey(H2_DELAY_KEY)) {
        if (!putUIntRobust(PROGRAM_NAMESPACE, H2_DELAY_KEY, transportDelaySec)) {
            Serial.println("Failed to seed program H2 delay key");
        }
    }
    if (nvsWritable && !prefs.isKey(H2_DELAY_KEY)) {
        if (!putUIntRobust(LED_CONFIG_NAMESPACE, H2_DELAY_KEY, transportDelaySec)) {
            Serial.println("Failed to seed led-config H2 delay key");
        }
    }
    if (transportDelaySec > 600) transportDelaySec = 600;
    state.hydrogenTransportDelaySeconds = static_cast<uint16_t>(transportDelaySec);

    uint32_t windStopRaw = programPrefs.getUInt(WIND_STOP_KEY, UINT32_MAX);
    if (windStopRaw == UINT32_MAX) {
        windStopRaw = prefs.getUInt(WIND_STOP_KEY, state.windStopSeconds);
    }
    uint16_t windStopSec = static_cast<uint16_t>(windStopRaw);
    if (windStopSec > 600) windStopSec = 600;
    if (nvsWritable && !programPrefs.isKey(WIND_STOP_KEY)) {
        if (!putUIntRobust(PROGRAM_NAMESPACE, WIND_STOP_KEY, windStopSec)) {
            Serial.println("Failed to seed program wind-stop key");
        }
    }
    if (nvsWritable && !prefs.isKey(WIND_STOP_KEY)) {
        if (!putUIntRobust(LED_CONFIG_NAMESPACE, WIND_STOP_KEY, windStopSec)) {
            Serial.println("Failed to seed led-config wind-stop key");
        }
    }
    state.windStopSeconds = windStopSec;

    uint32_t storageRunRaw = programPrefs.getUInt(STORAGE_RUN_KEY, UINT32_MAX);
    if (storageRunRaw == UINT32_MAX) {
        storageRunRaw = prefs.getUInt(STORAGE_RUN_KEY, state.storageRunSeconds);
    }
    uint16_t storageRunSec = static_cast<uint16_t>(storageRunRaw);
    if (storageRunSec > 600) storageRunSec = 600;
    if (nvsWritable && !programPrefs.isKey(STORAGE_RUN_KEY)) {
        if (!putUIntRobust(PROGRAM_NAMESPACE, STORAGE_RUN_KEY, storageRunSec)) {
            Serial.println("Failed to seed program storage-run key");
        }
    }
    if (nvsWritable && !prefs.isKey(STORAGE_RUN_KEY)) {
        if (!putUIntRobust(LED_CONFIG_NAMESPACE, STORAGE_RUN_KEY, storageRunSec)) {
            Serial.println("Failed to seed led-config storage-run key");
        }
    }
    state.storageRunSeconds = storageRunSec;

    uint32_t restartDelayRaw = programPrefs.getUInt(RESTART_DELAY_KEY, UINT32_MAX);
    if (restartDelayRaw == UINT32_MAX) {
        restartDelayRaw = prefs.getUInt(RESTART_DELAY_KEY, state.restartDelaySeconds);
    }
    uint16_t restartDelaySec = static_cast<uint16_t>(restartDelayRaw);
    if (restartDelaySec > 600) restartDelaySec = 600;
    if (nvsWritable && !programPrefs.isKey(RESTART_DELAY_KEY)) {
        if (!putUIntRobust(PROGRAM_NAMESPACE, RESTART_DELAY_KEY, restartDelaySec)) {
            Serial.println("Failed to seed program restart-delay key");
        }
    }
    if (nvsWritable && !prefs.isKey(RESTART_DELAY_KEY)) {
        if (!putUIntRobust(LED_CONFIG_NAMESPACE, RESTART_DELAY_KEY, restartDelaySec)) {
            Serial.println("Failed to seed led-config restart-delay key");
        }
    }
    state.restartDelaySeconds = restartDelaySec;

    // Load pin settings
    state.ledDataPin = loadPin("led_data_pin", state.ledDataPin);
    state.buttonPin = loadPin("button_pin", state.buttonPin);
    state.buttonLedPin = loadPin("button_led_pin", state.buttonLedPin);
    state.streetLedPin = loadPin("street_led_pin", state.streetLedPin);
    state.windRelayPin = loadOutputPin("wind_relay_pin", state.windRelayPin);
    state.electrolyserRelayPin = loadOutputPin(ELECTROLYSER_RELAY_PIN_KEY, state.electrolyserRelayPin);
    state.windInfoLedPin = loadPin("wind_info_pin", state.windInfoLedPin);
    state.electrolyserInfoLedPin = loadPin("electrolyser_info_pin", state.electrolyserInfoLedPin);
    state.hydrogenProductionInfoLedPin = loadPin("h2_prod_info_pin", state.hydrogenProductionInfoLedPin);
    state.hydrogenStorageInfoLedPin = loadPin("h2_storage_info_pin", state.hydrogenStorageInfoLedPin);
    state.hydrogenConsumptionInfoLedPin = loadPin("h2_cons_info_pin", state.hydrogenConsumptionInfoLedPin);
    state.electricityTransportInfoLedPin = loadPin("elec_tran_info_pin", state.electricityTransportInfoLedPin);
    state.streetInfoLedPin = loadPin("street_info_pin", state.streetInfoLedPin);

    // Load per-segment colors (defaults come from SystemState fields)
    state.windColor = loadColor("wind_color", state.windColor);
    state.solarColor = loadColor("solar_color", state.solarColor);
    state.hydrogenProductionColor = loadColor("h2_prod_color", state.hydrogenProductionColor);
    state.hydrogenTransportColor = loadColor("h2_trans_color", state.hydrogenTransportColor);
    state.hydrogenStorage1Color = loadColor("h2_stor1_color", state.hydrogenStorage1Color);
    state.hydrogenStorage2Color = loadColor("h2_stor2_color", state.hydrogenStorage2Color);
    state.h2ConsumptionColor = loadColor("h2_cons_color", state.h2ConsumptionColor);
    state.fabricationColor = loadColor("fabr_color", state.fabricationColor);
    state.electricityTransportColor = loadColor("elec_tran_color", state.electricityTransportColor);
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
    if (prefsOk && nvsWritable && !prefs.getBool("effects_v2", false)) {
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
        if (!putBoolRobust(LED_CONFIG_NAMESPACE, "effects_v2", true)) {
            Serial.println("effects_v2 migration flag write failed");
        }
    }
    // Non-running segments direction and delay (for running option)
    state.hydrogenProductionDirForward = loadDir("h2_prod_dir", true);
    state.fabricationDirForward = loadDir("fabr_dir", true);
    state.hydrogenProductionDelay = loadDelay("h2_prod_delay", LED_DELAY);
    state.fabricationDelay = loadDelay("fabr_delay", LED_DELAY);

    // Serve root page with all segments
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        int maxIndex = state.totalLeds > 0 ? state.totalLeds - 1 : 0;
        String maxIndexStr = String(maxIndex);
        String page;
        if (!page.reserve(24576)) {
            request->send(503, "text/plain", "Web UI tijdelijk niet beschikbaar (lage heap). Probeer opnieuw.");
            return;
        }
        page = "<html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>LED Segments</title>"
            "<style>body{font-family:Arial,sans-serif;max-width:900px;margin:20px auto;padding:10px;}"
            ".logo{text-align:center;margin:20px 0;}"
            ".logo img{max-width:200px;height:auto;}"
            "h3{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:5px;}"
            ".segment{background:#f9f9f9;padding:10px;margin:10px 0;border-radius:5px;}"
            "input{width:60px;padding:5px;margin:3px;}"
            "select{margin:3px;}"
            "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;margin:5px;}"
            "button:hover{background:#45a049;}"
            ".tabbar{display:flex;flex-wrap:wrap;gap:8px;margin:12px 0;}"
            ".tabbtn{background:#2f7ed8;padding:8px 12px;}"
            ".tabbtn:hover{background:#2365ad;}"
            ".tabbtn.active{background:#1f5da8;}"
            ".tab-panel{display:none;}"
            ".tab-panel.active{display:block;}"
            ".restart{background:#d9534f;}"
            ".restart:hover{background:#c9302c;}"
            ".test{background:#5bc0de;padding:8px 12px;}"
            ".test:hover{background:#46b8da;}"
            ".stop{background:#f0ad4e;}"
            ".stop:hover{background:#ec971f;}"
            ".line2{display:grid;grid-template-columns:auto auto auto;gap:6px;align-items:center;}"
            ".line3{display:flex;gap:10px;align-items:center;flex-wrap:wrap;}"
            ".status-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-bottom:1px solid #ddd;}"
            ".badge{padding:4px 10px;border-radius:12px;font-size:12px;font-weight:bold;}"
            ".badge.on{background:#4CAF50;color:#fff;}"
            ".badge.off{background:#bdbdbd;color:#333;}"
            "</style></head><body>"
            "<div class='logo'><img src='" + String(LOGO_DATA_URI) + "' alt='OakZo Logo'></div>"
            "<h3>Hydrogen Demo Control</h3>"
            "<div class='tabbar'>"
            "<button type='button' class='tabbtn active' data-tab='program' onclick=\"openTab('program')\">1. Program</button>"
            "<button type='button' class='tabbtn' data-tab='led' onclick=\"openTab('led')\">2. LED Settings</button>"
            "<button type='button' class='tabbtn' data-tab='pins' onclick=\"openTab('pins')\">3. Pin Settings</button>"
            "<button type='button' class='tabbtn' data-tab='timing' onclick=\"openTab('timing')\">4. Timing Settings</button>"
            "<button type='button' class='tabbtn' data-tab='status' onclick=\"openTab('status')\">5. Status</button>"
            "</div>";

    page += "<form id='saveForm' method=\"POST\" action=\"/update\" onsubmit=\"return false;\">";
    page += "<div id='tab-program' class='tab-panel active'>";
    if (state.testMode) {
        page += "<div style='background:#fff3cd;padding:15px;border-radius:5px;margin:10px 0;border:2px solid #ffc107;'>"
                "<b>LED LOOP TEST ACTIVE</b><br>Testing segment " + String(state.testSegmentStart) + "-" + String(state.testSegmentEnd) + "<br>"
                "<button type='button' class='stop' onclick=\"stopLedLoopTest()\">Stop Test</button>"
                "<button type='button' class='test' style='margin-left:8px;' onclick=\"startProgram()\">Start Program</button></div>";
    }
    page += "<div class='segment'><b>Program</b><br>"
        "<button type='button' class='test' onclick=\"startProgram()\">Start Program</button>"
        "<button type='button' class='test' onclick=\"startLedLoopTest()\">Start Test</button>"
        "<br><br>Auto-start program (disables manual button): "
        "<input type='checkbox' name='auto_start' value='1" + String(state.autoStartEnabled ? "' checked" : "'") + ">"
        "</div>";
    page += "</div>";

    page += "<div id='tab-pins' class='tab-panel'>";
    page += "<div class='segment'><b>Pin Settings</b><br>"
        "Wind relay pin: <input type='number' name='wind_relay_pin' min='0' max='39' value='" + String(state.windRelayPin) + "' style='width:70px;'><br>"
        "<button type='button' class='test' onclick=\"setRelay('wind','on')\">Wind ON</button>"
        " <button type='button' class='stop' onclick=\"setRelay('wind','off')\">Wind OFF</button><br><br>"
        "Electrolyser relay pin: <input type='number' name='electrolyser_relay_pin' min='0' max='39' value='" + String(state.electrolyserRelayPin) + "' style='width:70px;'><br>"
        "<button type='button' class='test' onclick=\"setRelay('electrolyser','on')\">Electrolyser ON</button>"
        " <button type='button' class='stop' onclick=\"setRelay('electrolyser','off')\">Electrolyser OFF</button><br>"
        "<small>Manual relay mode is reset automatically when the program starts.</small>"
        "<br><button type='button' onclick=\"savePinSettings()\">Save Pin Settings</button>"
        "</div>";
    page += "</div>";

    page += "<div id='tab-timing' class='tab-panel'>";
    page += "<div class='segment'><b>Timing Settings</b><br>"
        "Stop wind production (seconds): <input type='number' name='wind_stop_s' min='0' max='600' value='" + String(state.windStopSeconds) + "'><br>"
        "Delay after hydrogen production (seconds): <input type='number' name='h2_trans_delay_s' min='0' max='600' value='" + String(state.hydrogenTransportDelaySeconds) + "'><br>"
        "Hydrogen from storage runtime (seconds): <input type='number' name='storage_run_s' min='0' max='600' value='" + String(state.storageRunSeconds) + "'><br>"
        "Restart delay after storage (seconds): <input type='number' name='restart_delay_s' min='0' max='600' value='" + String(state.restartDelaySeconds) + "'><br>"
        "<button type='button' onclick=\"saveTimingSettings()\">Save Timing Settings</button>"
        "</div>";
    page += "</div>";
        
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
                "Start: <input id='" + String(startName) + "' type='number' name='" + String(startName) + "' min=0 max=" + maxIndexStr + " value=" + String(startVal) + ">"
                " End: <input id='" + String(endName) + "' type='number' name='" + String(endName) + "' min=0 max=" + maxIndexStr + " value=" + String(endVal) + ">" \
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
                "Start: <input id='" + String(startName) + "' type='number' name='" + String(startName) + "' min=0 max=" + maxIndexStr + " value=" + String(startVal) + ">"
                " End: <input id='" + String(endName) + "' type='number' name='" + String(endName) + "' min=0 max=" + maxIndexStr + " value=" + String(endVal) + ">" \
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
        
    page += "<div id='tab-led' class='tab-panel'>";
    page += "<div class='segment'><b>LED Settings</b><br>"
        "LED data pin: <input type='number' name='led_data_pin' min='0' max='39' value='" + String(state.ledDataPin) + "' style='width:70px;'><br>"
        "Total LEDs connected: <input type='number' name='total_leds' min='1' max='" + String(NUM_LEDS) + "' value='" + String(state.totalLeds) + "'>"
        "<br>Brightness divisor: <input id='brightness_divisor' type='range' name='brightness_divisor' min='1' max='10' value='" + String(state.brightnessDivisor) + "' oninput=\"updateBrightnessDivisorValue(this.value)\" style='width:220px;vertical-align:middle;'>"
        " <span id='brightness_divisor_value'>" + String(state.brightnessDivisor) + "</span>"
        "<br><small style='display:inline-block;width:220px;'><span style='float:left;'>1 = Geen dim</span><span style='float:right;'>10 = Sterk gedimd</span></small>"
        "<br><small>1 = 100% running brightness (head blijft altijd 100%).</small>"
        "<br><small>LED data pin is applied after restart.</small>"
        "</div>";
    addSegmentDir(state.windName.c_str(), "wind_name", "wind_start", "wind_end", "wind_dir", "wind_en", "wind_delay", "wind_eff", "wind_color", state.windColor, state.windSegmentStart, state.windSegmentEnd, state.windDirForward, state.windEnabled, state.windDelay, state.windEffectType);
    addSegmentDir(state.solarName.c_str(), "solar_name", "solar_start", "solar_end", "solar_dir", "solar_en", "solar_delay", "solar_eff", "solar_color", state.solarColor, state.solarSegmentStart, state.solarSegmentEnd, state.solarDirForward, state.solarEnabled, state.solarDelay, state.solarEffectType);
    addSegmentDir(state.hydrogenProductionName.c_str(), "h2_prod_name", "h2_prod_s", "h2_prod_e", "h2_prod_dir", "h2_prod_en", "h2_prod_delay", "h2_prod_eff", "h2_prod_color", state.hydrogenProductionColor, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd, state.hydrogenProductionDirForward, state.hydrogenProductionEnabled, state.hydrogenProductionDelay, state.hydrogenProductionEffectType);
    addSegmentDir(state.hydrogenTransportName.c_str(), "h2_trans_name", "h2_trans_s", "h2_trans_e", "h2_trans_dir", "h2_trans_en", "h2_trans_delay", "h2_trans_eff", "h2_trans_color", state.hydrogenTransportColor, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd, state.hydrogenTransportDirForward, state.hydrogenTransportEnabled, state.hydrogenTransportDelay, state.hydrogenTransportEffectType);
    addSegmentDir(state.hydrogenStorage1Name.c_str(), "h2_stor1_name", "h2_stor1_s", "h2_stor1_e", "h2_stor1_dir", "h2_stor_en", "h2_stor1_delay", "h2_stor1_eff", "h2_stor1_color", state.hydrogenStorage1Color, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd, state.hydrogenStorage1DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage1Delay, state.hydrogenStorage1EffectType);
    addSegmentDir(state.hydrogenStorage2Name.c_str(), "h2_stor2_name", "h2_stor2_s", "h2_stor2_e", "h2_stor2_dir", "h2_stor_en", "h2_stor2_delay", "h2_stor2_eff", "h2_stor2_color", state.hydrogenStorage2Color, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd, state.hydrogenStorage2DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage2Delay, state.hydrogenStorage2EffectType);
    addSegmentDir(state.h2ConsumptionName.c_str(), "h2_cons_name", "h2_cons_s", "h2_cons_e", "h2_cons_dir", "h2_cons_en", "h2_cons_delay", "h2_cons_eff", "h2_cons_color", state.h2ConsumptionColor, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd, state.h2ConsumptionDirForward, state.h2ConsumptionEnabled, state.h2ConsumptionDelay, state.h2ConsumptionEffectType);
    addSegmentSimple(state.fabricationName.c_str(), "fabr_name", "fabr_start", "fabr_end", "fabr_en", "fabr_eff", "fabr_dir", "fabr_delay", "fabr_color", state.fabricationColor, state.fabricationSegmentStart, state.fabricationSegmentEnd, state.fabricationEnabled, state.fabricationEffectType, state.fabricationDirForward, state.fabricationDelay);
    addSegmentDir(state.electricityTransportName.c_str(), "elec_tran_name", "elec_tran_s", "elec_tran_e", "elec_tran_dir", "elec_tran_en", "elec_tran_delay", "elec_tran_eff", "elec_tran_color", state.electricityTransportColor, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd, state.electricityTransportDirForward, state.electricityTransportEnabled, state.electricityTransportDelay, state.electricityTransportEffectType);
        addSegmentDir(state.storagePowerstationName.c_str(), "stor_pow_name", "stor_pow_s", "stor_pow_e", "stor_pow_dir", "stor_pow_en", "stor_pow_delay", "stor_pow_eff", "stor_pow_color", state.storagePowerstationColor, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd, state.storagePowerstationDirForward, state.storagePowerstationEnabled, state.storagePowerstationDelay, state.storagePowerstationEffectType);

        // Custom segments section
        page += "<h3>Custom Segments</h3>";
        
        for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
            auto &cs = state.custom[i];
            if (!cs.inUse) continue;
            String idx = String(i);
            // Render custom segment controls (same 3-line layout) + remove button
            page += "<div class='segment'><b>" + cs.name + "</b><br>";
            page += "Name: <input type='text' name='cust" + idx + "_name' value='" + cs.name + "' maxlength='32' style='width:180px;'>";
            page += "<br>Start: <input id='cust" + idx + "_s' type='number' name='cust" + idx + "_s' min=0 max=" + maxIndexStr + " value=" + String(cs.start) + ">";
            page += " End: <input id='cust" + idx + "_e' type='number' name='cust" + idx + "_e' min=0 max=" + maxIndexStr + " value=" + String(cs.end) + ">";
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


        page += "<button type='button' onclick=\"saveLedSettings()\">Save LED Settings</button></div></form>";

        auto statusBadge = [&](bool on) -> String {
            return String("<span class='badge ") + (on ? "on" : "off") + "'>" + (on ? "ACTIVE" : "OFF") + "</span>";
        };
        auto statusBadgeWithId = [&](bool on, const String& id) -> String {
            return String("<span id='") + id + "' class='badge " + (on ? "on" : "off") + "'>" + (on ? "ACTIVE" : "OFF") + "</span>";
        };

        page += "<div id='tab-status' class='tab-panel'>";
        page += "<div class='segment'><b>Program Status</b>";
        page += "<div class='status-row'><span>Program part</span><span id='st_program_part'>-</span></div>";
        page += "<div class='status-row'><span>Flow phase</span><span id='st_flow_phase'>-</span></div>";
        page += "<div class='status-row'><span>Program Active</span>" + statusBadgeWithId(state.generalTimerActive && !state.testMode, "st_program_active") + "</div>";
        page += "<div class='status-row'><span>Test mode</span>" + statusBadgeWithId(state.testMode, "st_test_mode") + "</div>";
        page += "</div>";

        page += "<div class='segment'><b>Segment Status</b>";
        page += "<div class='status-row'><span>" + state.windName + "</span>" + statusBadgeWithId(state.windEnabled && state.windOn, "st_wind") + "</div>";
        page += "<div class='status-row'><span>" + state.solarName + "</span>" + statusBadgeWithId(state.solarEnabled && state.solarOn, "st_solar") + "</div>";
        page += "<div class='status-row'><span>" + state.hydrogenProductionName + "</span>" + statusBadgeWithId(state.hydrogenProductionEnabled && state.hydrogenProductionOn, "st_h2_production") + "</div>";
        page += "<div class='status-row'><span>" + state.hydrogenTransportName + "</span>" + statusBadgeWithId(state.hydrogenTransportEnabled && state.hydrogenTransportOn, "st_h2_transport") + "</div>";
        page += "<div class='status-row'><span>" + state.hydrogenStorage1Name + "</span>" + statusBadgeWithId(state.hydrogenStorageEnabled && state.hydrogenStorageInOn, "st_h2_storage_in") + "</div>";
        page += "<div class='status-row'><span>" + state.hydrogenStorage2Name + "</span>" + statusBadgeWithId(state.hydrogenStorageEnabled && state.hydrogenStorageOutOn, "st_h2_storage_out") + "</div>";
        page += "<div class='status-row'><span>" + state.h2ConsumptionName + "</span>" + statusBadgeWithId(state.h2ConsumptionEnabled && state.h2ConsumptionOn, "st_h2_consumption") + "</div>";
        page += "<div class='status-row'><span>" + state.fabricationName + "</span>" + statusBadgeWithId(state.fabricationEnabled && state.fabricationOn, "st_fabrication") + "</div>";
        page += "<div class='status-row'><span>" + state.electricityTransportName + "</span>" + statusBadgeWithId(state.electricityTransportEnabled && state.electricityTransportOn, "st_electricity_transport") + "</div>";
        page += "<div class='status-row'><span>" + state.storagePowerstationName + "</span>" + statusBadgeWithId(state.storagePowerstationEnabled && state.storagePowerstationOn, "st_storage_powerstation") + "</div>";

        for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
            auto &cs = state.custom[i];
            if (!cs.inUse) continue;
            bool customOn = cs.enabled && EffectUtils::isTriggerActive(state, cs.trigger);
            page += "<div class='status-row'><span>" + cs.name + "</span>" + statusBadgeWithId(customOn, String("st_custom_") + String(i)) + "</div>";
        }

        page += "</div>"
            "<div class='segment'><b>Relay Status</b>";
        bool windRelayActive = state.windRelayOutputOn;
        bool electrolyserRelayActive = state.electrolyserRelayOutputOn;
        page += "<div class='status-row'><span>Wind Relay</span>" + statusBadgeWithId(windRelayActive, "st_wind_relay") + "</div>";
        page += "<div class='status-row'><span>Electrolyser Relay</span>" + statusBadgeWithId(electrolyserRelayActive, "st_electrolyser_relay") + "</div>";
        page += "</div>"
            "</div><hr>"
            "<script>\n"
            "function openTab(tabId){\n"
            "  document.querySelectorAll('.tab-panel').forEach(el=>el.classList.remove('active'));\n"
            "  document.querySelectorAll('.tabbtn').forEach(el=>el.classList.remove('active'));\n"
            "  const panel = document.getElementById('tab-'+tabId);\n"
            "  const btn = document.querySelector('.tabbtn[data-tab=\"'+tabId+'\"]');\n"
            "  if(panel) panel.classList.add('active');\n"
            "  if(btn) btn.classList.add('active');\n"
            "}\n"
            "function updateBrightnessDivisorValue(v){\n"
            "  const el=document.getElementById('brightness_divisor_value');\n"
            "  if(el) el.textContent = String(v);\n"
            "}\n"
            "function setStatusBadge(id,on){\n"
            "  const el=document.getElementById(id);\n"
            "  if(!el) return;\n"
            "  el.classList.toggle('on', !!on);\n"
            "  el.classList.toggle('off', !on);\n"
            "  el.textContent = on ? 'ACTIVE' : 'OFF';\n"
            "}\n"
            "function setStatusText(id,value){\n"
            "  const el=document.getElementById(id);\n"
            "  if(!el) return;\n"
            "  el.textContent = value || '-';\n"
            "}\n"
            "function refreshStatusBadges(){\n"
            "  fetch('/status_data')\n"
            "    .then(r=>{ if(!r.ok) throw new Error('status fetch failed'); return r.json(); })\n"
            "    .then(s=>{\n"
            "      setStatusText('st_flow_phase', s.flow_phase);\n"
            "      setStatusText('st_program_part', s.program_part);\n"
            "      setStatusBadge('st_program_active', s.program_active);\n"
            "      setStatusBadge('st_test_mode', s.test_mode);\n"
            "      setStatusBadge('st_wind', s.wind);\n"
            "      setStatusBadge('st_solar', s.solar);\n"
            "      setStatusBadge('st_h2_production', s.h2_production);\n"
            "      setStatusBadge('st_h2_transport', s.h2_transport);\n"
            "      setStatusBadge('st_h2_storage_in', s.h2_storage_in);\n"
            "      setStatusBadge('st_h2_storage_out', s.h2_storage_out);\n"
            "      setStatusBadge('st_h2_consumption', s.h2_consumption);\n"
            "      setStatusBadge('st_fabrication', s.fabrication);\n"
            "      setStatusBadge('st_electricity_transport', s.electricity_transport);\n"
            "      setStatusBadge('st_storage_powerstation', s.storage_powerstation);\n"
            "      setStatusBadge('st_wind_relay', s.wind_relay);\n"
            "      setStatusBadge('st_electrolyser_relay', s.electrolyser_relay);\n"
            "      for(let i=0;i<8;i++){\n"
            "        const key='custom_'+i;\n"
            "        if(Object.prototype.hasOwnProperty.call(s,key)){\n"
            "          setStatusBadge('st_'+key, s[key]);\n"
            "        }\n"
            "      }\n"
            "    })\n"
            "    .catch(()=>{});\n"
            "}\n"
            "setInterval(refreshStatusBadges, 2000);\n"
            "refreshStatusBadges();\n"
            "function addCustomSegment(){\n"
            "  fetch('/add_custom',{method:'POST'})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Failed to add custom segment'));\n"
            "}\n"
            "function startLedLoopTest(){\n"
            "  fetch('/start_test_all',{method:'POST'})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Test start failed'));\n"
            "}\n"
            "function stopLedLoopTest(){\n"
            "  fetch('/stoptest',{method:'POST'})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Stop test failed'));\n"
            "}\n"
            "function removeCustomSegment(id){\n"
            "  const body=new URLSearchParams({id:id}).toString();\n"
            "  fetch('/remove_custom',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Failed to remove custom segment'));\n"
            "}\n"
            "function setRelay(relay,state){\n"
            "  let body;\n"
            "  if(relay==='auto'){ body = new URLSearchParams({relay:'auto',state:'auto'}).toString(); }\n"
            "  else {\n"
            "    const pinName = relay==='wind' ? 'wind_relay_pin' : 'electrolyser_relay_pin';\n"
            "    const pinInput = document.querySelector(\"input[name='\"+pinName+\"']\");\n"
            "    if(!pinInput){ alert('Relay pin input not found'); return; }\n"
            "    body = new URLSearchParams({relay: relay, state: state, pin: pinInput.value}).toString();\n"
            "  }\n"
            "  fetch('/set_relay',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
            "    .then(r=>{ if(!r.ok){ return r.text().then(t=>{ throw new Error(t||'Relay test failed'); }); } return r.text(); })\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(e=>alert(e.message || 'Relay test failed'));\n"
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
            "function startProgram(){\n"
            "  fetch('/start_program',{method:'POST'})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Program start failed'));\n"
            "}\n"
            "function startProcessChain(){\n"
            "  fetch('/start_chain',{method:'POST'})\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(()=>alert('Process chain start failed'));\n"
            "}\n"
            "function saveLedSettings(){\n"
            "  const form = document.getElementById('saveForm');\n"
            "  if(!form){ alert('Settings form not found'); return; }\n"
            "  const fd = new FormData(form);\n"
            "  fd.append('save_mode','led');\n"
            "  const body = new URLSearchParams();\n"
            "  for (const [k,v] of fd.entries()) body.append(k, v);\n"
            "  fetch('/update',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body.toString()})\n"
            "    .then(r=>{ if(!r.ok){ return r.text().then(t=>{ throw new Error(t||'LED save failed'); }); } })\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(e=>alert(e.message || 'LED save failed'));\n"
            "}\n"
            "function savePinSettings(){\n"
            "  const windPin = document.querySelector(\"input[name='wind_relay_pin']\");\n"
            "  const elyPin = document.querySelector(\"input[name='electrolyser_relay_pin']\");\n"
            "  if(!windPin || !elyPin){ alert('Pin inputs not found'); return; }\n"
            "  const body = new URLSearchParams({wind_relay_pin: windPin.value, electrolyser_relay_pin: elyPin.value}).toString();\n"
            "  fetch('/update_pins',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
            "    .then(r=>{ if(!r.ok){ return r.text().then(t=>{ throw new Error(t||'Pin save failed'); }); } })\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(e=>alert(e.message || 'Pin save failed'));\n"
            "}\n"
            "function saveTimingSettings(){\n"
            "  const wind = document.querySelector(\"input[name='wind_stop_s']\");\n"
            "  const h2 = document.querySelector(\"input[name='h2_trans_delay_s']\");\n"
            "  const storageRun = document.querySelector(\"input[name='storage_run_s']\");\n"
            "  const restartDelay = document.querySelector(\"input[name='restart_delay_s']\");\n"
            "  if(!wind || !h2 || !storageRun || !restartDelay){ alert('Timing inputs not found'); return; }\n"
            "  const body = new URLSearchParams({wind_stop_s: wind.value, h2_trans_delay_s: h2.value, storage_run_s: storageRun.value, restart_delay_s: restartDelay.value}).toString();\n"
            "  fetch('/update_timing',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body})\n"
            "    .then(r=>{ if(!r.ok){ return r.text().then(t=>{ throw new Error(t||'Timing save failed'); }); } })\n"
            "    .then(()=>window.location.reload())\n"
            "    .catch(e=>alert(e.message || 'Timing save failed'));\n"
            "}\n"
            "const bd=document.getElementById('brightness_divisor');\n"
            "if(bd) updateBrightnessDivisorValue(bd.value);\n"
            
            "</script>"
            "<form method='POST' action='/restart' onsubmit=\"return confirm('Restart the device?')\">"
            "<button type='submit' class='restart'>Restart ESP</button></form></body></html>";

        AsyncResponseStream *response = request->beginResponseStream("text/html");
        response->print(page);
        request->send(response);
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
           page += "<div style='margin:15px 0;'></div>";
        
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
            showTriggerStatus("Fabrication Direct", TriggerType::H2_CONSUMPTION);
            showTriggerStatus("Fabrication", TriggerType::FABRICATION);
            showTriggerStatus("Fabrication Storage", TriggerType::ELECTRICITY_TRANSPORT);
            showTriggerStatus("Storage Powerstation", TriggerType::STORAGE_POWERSTATION);
        
            page += "</body></html>";
            request->send(200, "text/html", page);
        });

        server.on("/status_data", HTTP_GET, [](AsyncWebServerRequest *request){
            String json = "{";
            auto addBool = [&](const String& key, bool value) {
                if (json.length() > 1) json += ",";
                json += "\"" + key + "\":" + String(value ? "true" : "false");
            };
            auto addString = [&](const String& key, const String& value) {
                if (json.length() > 1) json += ",";
                String safe = value;
                safe.replace("\\", "\\\\");
                safe.replace("\"", "\\\"");
                json += "\"" + key + "\":\"" + safe + "\"";
            };

            String flowPhase = "Idle";
            String programPart = "Idle";
            if (state.testMode) {
                flowPhase = "Test mode";
                programPart = "Test mode";
            } else if (state.restartDelayActive) {
                programPart = "Restart delay";
                flowPhase = "Waiting restart delay";
            } else if (state.emptyPipe) {
                programPart = "Hydrogen from storage";
                if (state.activeProgram == ProgramVariant::HYDROGEN_FROM_STORAGE && state.hydrogenStorageOutOn) {
                    if (state.hydrogenTransportOn) {
                        flowPhase = "Hydrogen from storage - Storage Out running + Draining Transport";
                    } else if (state.hydrogenStorageInOn) {
                        flowPhase = "Hydrogen from storage - Storage Out running + Draining Storage In";
                    } else {
                        flowPhase = "Hydrogen from storage - Storage Out running";
                    }
                } else if (state.hydrogenTransportOn) {
                    flowPhase = "Wind stop - draining Hydrogen Transport";
                } else if (state.hydrogenStorageInOn) {
                    flowPhase = "Wind stop - draining Hydrogen Storage In";
                } else {
                    flowPhase = "Wind stop - drain complete";
                }
            } else if (state.generalTimerActive) {
                if (state.activeProgram == ProgramVariant::HYDROGEN_FROM_STORAGE) {
                    programPart = "Hydrogen from storage";
                    if (state.hydrogenStorageOutOn) {
                        flowPhase = "Hydrogen from storage - Storage Out running";
                    } else if (state.h2ConsumptionOn) {
                        flowPhase = "Hydrogen from storage - Fabrication Direct running";
                    } else if (state.fabricationOn) {
                        flowPhase = "Hydrogen from storage - Fabrication Fire running";
                    } else {
                        flowPhase = "Hydrogen from storage - waiting";
                    }
                } else if (state.windOn && !state.solarOn) {
                    programPart = "Hydrogen from renewable";
                    flowPhase = "Wind running";
                } else if (state.windOn && state.solarOn && !state.hydrogenTransportOn && state.hydrogenTransportDelayActive) {
                    programPart = "Hydrogen from renewable";
                    flowPhase = "Waiting Hydrogen Transport delay";
                } else if (state.hydrogenTransportOn && !state.hydrogenStorageInOn) {
                    programPart = "Hydrogen from renewable";
                    flowPhase = "Hydrogen Transport running";
                } else if (state.hydrogenStorageInOn) {
                    programPart = "Hydrogen from renewable";
                    flowPhase = "Hydrogen Storage In running";
                } else if (state.h2ConsumptionOn) {
                    programPart = "Hydrogen from renewable";
                    flowPhase = "Fabrication Direct running";
                } else if (state.fabricationOn) {
                    programPart = "Hydrogen from renewable";
                    flowPhase = "Fabrication Fire running";
                } else {
                    programPart = "Hydrogen from renewable";
                    flowPhase = "Program active";
                }
            }

            addString("flow_phase", flowPhase);
            addString("program_part", programPart);

            addBool("program_active", state.generalTimerActive && !state.testMode);
            addBool("test_mode", state.testMode);
            addBool("wind", state.windEnabled && state.windOn);
            addBool("solar", state.solarEnabled && state.solarOn);
            addBool("h2_production", state.hydrogenProductionEnabled && state.hydrogenProductionOn);
            addBool("h2_transport", state.hydrogenTransportEnabled && state.hydrogenTransportOn);
            addBool("h2_storage_in", state.hydrogenStorageEnabled && state.hydrogenStorageInOn);
            addBool("h2_storage_out", state.hydrogenStorageEnabled && state.hydrogenStorageOutOn);
            addBool("h2_consumption", state.h2ConsumptionEnabled && state.h2ConsumptionOn);
            addBool("fabrication", state.fabricationEnabled && state.fabricationOn);
            addBool("electricity_transport", state.electricityTransportEnabled && state.electricityTransportOn);
            addBool("storage_powerstation", state.storagePowerstationEnabled && state.storagePowerstationOn);
            addBool("wind_relay", state.windRelayOutputOn);
            addBool("electrolyser_relay", state.electrolyserRelayOutputOn);

            for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
                auto &cs = state.custom[i];
                if (!cs.inUse) continue;
                bool customOn = cs.enabled && EffectUtils::isTriggerActive(state, cs.trigger);
                addBool(String("custom_") + String(i), customOn);
            }

            json += "}";
            request->send(200, "application/json", json);
        });

        server.on("/reset_loop", HTTP_POST, [](AsyncWebServerRequest *request){
            resetAllVariables();
            state.windOn = false;
            state.buttonDisabled = false;
            state.generalTimerActive = false;
            digitalWrite(state.buttonLedPin, HIGH);
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
                    int maxIndex = state.totalLeds > 0 ? state.totalLeds - 1 : 0;
                    state.custom[i].end = min(9, maxIndex);
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
        String saveMode = "full";
        if (request->hasParam("save_mode", true)) {
            saveMode = request->getParam("save_mode", true)->value();
            saveMode.trim();
            saveMode.toLowerCase();
        }
        bool ledOnly = (saveMode == "led");

        uint16_t newLedCount = state.totalLeds;
        if (request->hasParam("total_leds", true)) {
            int requested = request->getParam("total_leds", true)->value().toInt();
            if (requested < 1 || requested > NUM_LEDS) {
                request->send(400, "text/plain", "Invalid LED count");
                return;
            }
            newLedCount = static_cast<uint16_t>(requested);
        }

        // Helper to get and validate segment parameters
        auto getSegment = [&](const char* startName, const char* endName, int &outStart, int &outEnd) -> bool {
            if (!request->hasParam(startName, true) || !request->hasParam(endName, true)) return false;
            int s = request->getParam(startName, true)->value().toInt();
            int e = request->getParam(endName, true)->value().toInt();
            if (s < 0 || s >= newLedCount || e < 0 || e >= newLedCount || s > e) return false;
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
        auto packColorLocal = [](CRGB c) -> uint32_t {
            return (static_cast<uint32_t>(c.r) << 16) |
                   (static_cast<uint32_t>(c.g) << 8) |
                   static_cast<uint32_t>(c.b);
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
        auto getPin = [&](const char* name, uint8_t &outPin) -> bool {
            if (!request->hasParam(name, true)) return false;
            int v = request->getParam(name, true)->value().toInt();
            if (v < 0 || v > 39) return false;
            outPin = static_cast<uint8_t>(v);
            return true;
        };
        auto getOutputPin = [&](const char* name, uint8_t &outPin) -> bool {
            if (!getPin(name, outPin)) return false;
            return isSafeOutputGpio(outPin);
        };
        auto getLedDataPin = [&](const char* name, uint8_t &outPin) -> bool {
            if (!getPin(name, outPin)) return false;
            switch (outPin) {
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
        };

    int ws, we, ss, se, hps, hpe, hts, hte, h1s, h1e, h2s, h2e, hcs, hce, fs, fe, ets, ete, sps, spe;
    bool wdir, sdir, hpdir, htdir, h1dir, h2dir, hcdir, etdir, spdir, fbdir;
    int wdly, sdly, hpdly, htdly, h1dly, h2dly, hcdly, etdly, spdly, fbdly;
    int weff, seff, hpeff, hteff, h1eff, h2eff, hceff, eteff, speff;
    int fbeff; // fabrication effect
    uint8_t ledDataPin, windRelayPin, electrolyserRelayPin;
    uint32_t wcol, scol, hpcol, htcol, hs1col, hs2col, hccol, fbcol, etcol, spcol;
    String wname, sname, hpname, htname, hs1name, hs2name, hcname, fbname, etname, spname;
    uint16_t h2delaySeconds, windStopSeconds, storageRunSeconds, restartDelaySeconds;
    int brightnessDivisor = state.brightnessDivisor;

        if (request->hasParam("brightness_divisor", true)) {
            brightnessDivisor = request->getParam("brightness_divisor", true)->value().toInt();
            if (brightnessDivisor < 1 || brightnessDivisor > 10) {
                request->send(400, "text/plain", "Invalid brightness divisor");
                return;
            }
        }

        hts = state.hydrogenTransportSegmentStart;
        hte = state.hydrogenTransportSegmentEnd;
        htdir = state.hydrogenTransportDirForward;
        htdly = state.hydrogenTransportDelay;
        hteff = state.hydrogenTransportEffectType;
        htcol = ((uint32_t)state.hydrogenTransportColor.r << 16) | ((uint32_t)state.hydrogenTransportColor.g << 8) | (uint32_t)state.hydrogenTransportColor.b;
        htname = state.hydrogenTransportName;
        
        if (!getSegment("wind_start", "wind_end", ws, we) ||
            !getSegment("solar_start", "solar_end", ss, se) ||
            !getSegment("h2_prod_s", "h2_prod_e", hps, hpe) ||
            !getSegment("h2_trans_s", "h2_trans_e", hts, hte) ||
            !getSegment("h2_stor1_s", "h2_stor1_e", h1s, h1e) ||
            !getSegment("h2_stor2_s", "h2_stor2_e", h2s, h2e) ||
            !getSegment("h2_cons_s", "h2_cons_e", hcs, hce) ||
            !getSegment("fabr_start", "fabr_end", fs, fe) ||
            !getSegment("elec_tran_s", "elec_tran_e", ets, ete) ||
            !getSegment("stor_pow_s", "stor_pow_e", sps, spe) ||
            !getDir("wind_dir", wdir) ||
            !getDir("solar_dir", sdir) ||
            !getDir("h2_prod_dir", hpdir) ||
            !getDir("h2_trans_dir", htdir) ||
            !getDir("h2_stor1_dir", h1dir) ||
            !getDir("h2_stor2_dir", h2dir) ||
            !getDir("h2_cons_dir", hcdir) ||
            !getDir("elec_tran_dir", etdir) ||
            !getDir("stor_pow_dir", spdir) ||
            !getDir("fabr_dir", fbdir) ||
            !getEffect3("wind_eff", weff) ||
            !getEffect3("solar_eff", seff) ||
            !getEffect3("h2_prod_eff", hpeff) ||
            !getEffect3("h2_trans_eff", hteff) ||
            !getEffect3("h2_stor1_eff", h1eff) ||
            !getEffect3("h2_stor2_eff", h2eff) ||
            !getEffect3("h2_cons_eff", hceff) ||
            !getEffect3("elec_tran_eff", eteff) ||
            !getEffect3("stor_pow_eff", speff) ||
            !getEffect3("fabr_eff", fbeff) ||
            !getLedDataPin("led_data_pin", ledDataPin) ||
            !getOutputPin("wind_relay_pin", windRelayPin) ||
            !getOutputPin("electrolyser_relay_pin", electrolyserRelayPin) ||
            // Colors must be present when saving
            !request->hasParam("wind_color", true) ||
            !request->hasParam("solar_color", true) ||
            !request->hasParam("h2_prod_color", true) ||
            !request->hasParam("h2_trans_color", true) ||
            !request->hasParam("h2_stor1_color", true) ||
            !request->hasParam("h2_stor2_color", true) ||
            !request->hasParam("h2_cons_color", true) ||
            !request->hasParam("fabr_color", true) ||
            !request->hasParam("elec_tran_color", true) ||
            !request->hasParam("stor_pow_color", true) ||
            !getDelay("wind_delay", wdly) ||
            !getDelay("solar_delay", sdly) ||
            !getDelay("h2_prod_delay", hpdly) ||
            !getDelay("h2_trans_delay", htdly) ||
            !getDelay("h2_stor1_delay", h1dly) ||
            !getDelay("h2_stor2_delay", h2dly) ||
            !getDelay("h2_cons_delay", hcdly) ||
            !getDelay("elec_tran_delay", etdly) ||
            !getDelay("stor_pow_delay", spdly) ||
            !getDelay("fabr_delay", fbdly) ||
            !getDelaySeconds("wind_stop_s", windStopSeconds) ||
            !getDelaySeconds("h2_trans_delay_s", h2delaySeconds) ||
            !getDelaySeconds("storage_run_s", storageRunSeconds) ||
            !getDelaySeconds("restart_delay_s", restartDelaySeconds) ||
            !getName("wind_name", wname) ||
            !getName("solar_name", sname) ||
            !getName("h2_prod_name", hpname) ||
            !getName("h2_trans_name", htname) ||
            !getName("h2_stor1_name", hs1name) ||
            !getName("h2_stor2_name", hs2name) ||
            !getName("h2_cons_name", hcname) ||
            !getName("fabr_name", fbname) ||
            !getName("elec_tran_name", etname) ||
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
            !parseHexColor("h2_prod_color", hpcol) ||
            !parseHexColor("h2_trans_color", htcol) ||
            !parseHexColor("h2_stor1_color", hs1col) ||
            !parseHexColor("h2_stor2_color", hs2col) ||
            !parseHexColor("h2_cons_color", hccol) ||
            !parseHexColor("fabr_color", fbcol) ||
            !parseHexColor("elec_tran_color", etcol) ||
            !parseHexColor("stor_pow_color", spcol)) {
            request->send(400, "text/plain", "Invalid color format");
            return;
        }

        // Checkboxes (missing means false)
        bool wen  = getCheckbox("wind_en");
        bool sen  = getCheckbox("solar_en");
        bool hpen = getCheckbox("h2_prod_en");
        bool hten = getCheckbox("h2_trans_en");
        bool hsen = getCheckbox("h2_stor_en");
        bool hcen = getCheckbox("h2_cons_en");
        bool fben = getCheckbox("fabr_en");
        bool eten = getCheckbox("elec_tran_en");
        bool spen = getCheckbox("stor_pow_en");
        bool elyen = state.electrolyserEnabled;
        bool autoStart = getCheckbox("auto_start");

        // Build a temporary list of enabled segments (built-ins + in-use customs) to check for overlap BEFORE saving anything
        struct RangeItem { String name; int s; int e; };
        RangeItem ranges[20];
        int rc = 0;
        auto addRange = [&](const String &nm, int s, int e, bool en){ if(en && rc < 20){ ranges[rc++] = RangeItem{nm, s, e}; } };
        // Built-ins
        addRange(wname, ws, we, wen);
        addRange(sname, ss, se, sen);
        addRange(htname, hts, hte, hten);
        addRange(hs1name, h1s, h1e, hsen);
        addRange(hs2name, h2s, h2e, hsen);
        addRange(hcname, hcs, hce, hcen);
        addRange(fbname, fs, fe, fben);
        addRange(etname, ets, ete, eten);
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
            int maxIndex = newLedCount > 0 ? newLedCount - 1 : 0;
            if (t.s < 0) t.s = 0;
            if (t.s > maxIndex) t.s = maxIndex;
            if (t.e < 0) t.e = 0;
            if (t.e > maxIndex) t.e = maxIndex;
            if (t.s>t.e) t.e=t.s;
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

        bool persistFailed = false;
        String persistFailedKey;
        auto persist = [&](bool ok, const char* key) {
            if (!ok) {
                if (!persistFailed) {
                    Serial.printf("Persistence failed in /update at key: %s\n", key);
                    persistFailedKey = key;
                }
                persistFailed = true;
            }
            return ok;
        };
        auto sendPersistFailure = [&]() {
            String msg = String("Failed to persist settings at key: ") + (persistFailedKey.length() ? persistFailedKey : String("unknown"));
            request->send(500, "text/plain", msg);
        };

        // Save all to preferences
        auto saveBuiltInSegment = [&](const char* nameKey, const char* startKey, const char* endKey,
                                      const char* dirKey, const char* enKey, const char* delayKey,
                                      const char* effKey, const char* colorKey,
                                      const String &name, int start, int end,
                                      bool dir, bool en, int delay, int eff, uint32_t color,
                                      const String &curName, int curStart, int curEnd,
                                      bool curDir, bool curEn, int curDelay, int curEff, uint32_t curColor) {
            if (persistFailed) return;
            if (name != curName) {
                persist(putStringRobust(LED_CONFIG_NAMESPACE, nameKey, name), nameKey);
                if (persistFailed) return;
            }
            if (en != curEn) {
                persist(putBoolRobust(LED_CONFIG_NAMESPACE, enKey, en), enKey);
            }
            if (persistFailed) return;
            if (start != curStart) {
                persist(putIntRobust(LED_CONFIG_NAMESPACE, startKey, start), startKey);
            }
            if (persistFailed) return;
            if (end != curEnd) {
                persist(putIntRobust(LED_CONFIG_NAMESPACE, endKey, end), endKey);
            }
            if (persistFailed) return;
            if (dir != curDir) {
                persist(putBoolRobust(LED_CONFIG_NAMESPACE, dirKey, dir), dirKey);
            }
            if (persistFailed) return;
            if (delay != curDelay) {
                persist(putIntRobust(LED_CONFIG_NAMESPACE, delayKey, delay), delayKey);
            }
            if (persistFailed) return;
            if (color != curColor) {
                persist(putUIntRobust(LED_CONFIG_NAMESPACE, colorKey, color), colorKey);
            }
            if (persistFailed) return;
            if (eff != curEff) {
                Serial.printf("Saving effect: key=%s newVal=%d oldVal=%d\n", effKey, eff, curEff);
                persist(putIntRobust(LED_CONFIG_NAMESPACE, effKey, eff), effKey);
            }
        };

        saveBuiltInSegment("wind_name", "wind_start", "wind_end", "wind_dir", "wind_en", "wind_delay", "wind_eff", "wind_color", wname, ws, we, wdir, wen, wdly, weff, wcol, state.windName, state.windSegmentStart, state.windSegmentEnd, state.windDirForward, state.windEnabled, state.windDelay, state.windEffectType, packColorLocal(state.windColor));
        saveBuiltInSegment("solar_name", "solar_start", "solar_end", "solar_dir", "solar_en", "solar_delay", "solar_eff", "solar_color", sname, ss, se, sdir, sen, sdly, seff, scol, state.solarName, state.solarSegmentStart, state.solarSegmentEnd, state.solarDirForward, state.solarEnabled, state.solarDelay, state.solarEffectType, packColorLocal(state.solarColor));
        saveBuiltInSegment("h2_prod_name", "h2_prod_s", "h2_prod_e", "h2_prod_dir", "h2_prod_en", "h2_prod_delay", "h2_prod_eff", "h2_prod_color", hpname, hps, hpe, hpdir, hpen, hpdly, hpeff, hpcol, state.hydrogenProductionName, state.hydrogenProductionSegmentStart, state.hydrogenProductionSegmentEnd, state.hydrogenProductionDirForward, state.hydrogenProductionEnabled, state.hydrogenProductionDelay, state.hydrogenProductionEffectType, packColorLocal(state.hydrogenProductionColor));
        saveBuiltInSegment("h2_trans_name", "h2_trans_s", "h2_trans_e", "h2_trans_dir", "h2_trans_en", "h2_trans_delay", "h2_trans_eff", "h2_trans_color", htname, hts, hte, htdir, hten, htdly, hteff, htcol, state.hydrogenTransportName, state.hydrogenTransportSegmentStart, state.hydrogenTransportSegmentEnd, state.hydrogenTransportDirForward, state.hydrogenTransportEnabled, state.hydrogenTransportDelay, state.hydrogenTransportEffectType, packColorLocal(state.hydrogenTransportColor));
        saveBuiltInSegment("h2_stor1_name", "h2_stor1_s", "h2_stor1_e", "h2_stor1_dir", "h2_stor_en", "h2_stor1_delay", "h2_stor1_eff", "h2_stor1_color", hs1name, h1s, h1e, h1dir, hsen, h1dly, h1eff, hs1col, state.hydrogenStorage1Name, state.hydrogenStorage1SegmentStart, state.hydrogenStorage1SegmentEnd, state.hydrogenStorage1DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage1Delay, state.hydrogenStorage1EffectType, packColorLocal(state.hydrogenStorage1Color));
        saveBuiltInSegment("h2_stor2_name", "h2_stor2_s", "h2_stor2_e", "h2_stor2_dir", "h2_stor_en", "h2_stor2_delay", "h2_stor2_eff", "h2_stor2_color", hs2name, h2s, h2e, h2dir, hsen, h2dly, h2eff, hs2col, state.hydrogenStorage2Name, state.hydrogenStorage2SegmentStart, state.hydrogenStorage2SegmentEnd, state.hydrogenStorage2DirForward, state.hydrogenStorageEnabled, state.hydrogenStorage2Delay, state.hydrogenStorage2EffectType, packColorLocal(state.hydrogenStorage2Color));
        saveBuiltInSegment("h2_cons_name", "h2_cons_s", "h2_cons_e", "h2_cons_dir", "h2_cons_en", "h2_cons_delay", "h2_cons_eff", "h2_cons_color", hcname, hcs, hce, hcdir, hcen, hcdly, hceff, hccol, state.h2ConsumptionName, state.hydrogenConsumptionSegmentStart, state.hydrogenConsumptionSegmentEnd, state.h2ConsumptionDirForward, state.h2ConsumptionEnabled, state.h2ConsumptionDelay, state.h2ConsumptionEffectType, packColorLocal(state.h2ConsumptionColor));
        saveBuiltInSegment("fabr_name", "fabr_start", "fabr_end", "fabr_dir", "fabr_en", "fabr_delay", "fabr_eff", "fabr_color", fbname, fs, fe, fbdir, fben, fbdly, fbeff, fbcol, state.fabricationName, state.fabricationSegmentStart, state.fabricationSegmentEnd, state.fabricationDirForward, state.fabricationEnabled, state.fabricationDelay, state.fabricationEffectType, packColorLocal(state.fabricationColor));
        saveBuiltInSegment("elec_tran_name", "elec_tran_s", "elec_tran_e", "elec_tran_dir", "elec_tran_en", "elec_tran_delay", "elec_tran_eff", "elec_tran_color", etname, ets, ete, etdir, eten, etdly, eteff, etcol, state.electricityTransportName, state.electricityTransportSegmentStart, state.electricityTransportSegmentEnd, state.electricityTransportDirForward, state.electricityTransportEnabled, state.electricityTransportDelay, state.electricityTransportEffectType, packColorLocal(state.electricityTransportColor));
        saveBuiltInSegment("stor_pow_name", "stor_pow_s", "stor_pow_e", "stor_pow_dir", "stor_pow_en", "stor_pow_delay", "stor_pow_eff", "stor_pow_color", spname, sps, spe, spdir, spen, spdly, speff, spcol, state.storagePowerstationName, state.storagePowerstationSegmentStart, state.storagePowerstationSegmentEnd, state.storagePowerstationDirForward, state.storagePowerstationEnabled, state.storagePowerstationDelay, state.storagePowerstationEffectType, packColorLocal(state.storagePowerstationColor));
        if (persistFailed) {
            sendPersistFailure();
            return;
        }

    // Update runtime state
        state.windName = wname;
        state.solarName = sname;
        state.hydrogenProductionName = hpname;
        state.hydrogenTransportName = htname;
        state.hydrogenStorage1Name = hs1name;
        state.hydrogenStorage2Name = hs2name;
        state.h2ConsumptionName = hcname;
        state.fabricationName = fbname;
        state.electricityTransportName = etname;
        state.storagePowerstationName = spname;
        state.windSegmentStart = ws; state.windSegmentEnd = we;
        state.solarSegmentStart = ss; state.solarSegmentEnd = se;
        state.hydrogenProductionSegmentStart = hps; state.hydrogenProductionSegmentEnd = hpe;
        state.hydrogenProductionEnabled = hpen;
        state.hydrogenProductionDirForward = hpdir;
        state.hydrogenProductionDelay = hpdly;
        state.hydrogenProductionEffectType = hpeff;
        state.hydrogenProductionColor = CRGB((hpcol >> 16) & 0xFF, (hpcol >> 8) & 0xFF, hpcol & 0xFF);
        state.hydrogenTransportSegmentStart = hts; state.hydrogenTransportSegmentEnd = hte;
        state.hydrogenStorage1SegmentStart = h1s; state.hydrogenStorage1SegmentEnd = h1e;
        state.hydrogenStorage2SegmentStart = h2s; state.hydrogenStorage2SegmentEnd = h2e;
        state.hydrogenConsumptionSegmentStart = hcs; state.hydrogenConsumptionSegmentEnd = hce;
        state.fabricationSegmentStart = fs; state.fabricationSegmentEnd = fe;
        state.electricityTransportSegmentStart = ets; state.electricityTransportSegmentEnd = ete;
        state.storagePowerstationSegmentStart = sps; state.storagePowerstationSegmentEnd = spe;
        state.brightnessDivisor = brightnessDivisor;
        state.ledDataPin = ledDataPin;
        if (!ledOnly) {
            state.windRelayPin = windRelayPin;
            state.electrolyserRelayPin = electrolyserRelayPin;
        }
        // Do not reconfigure IO pins while serving this request; apply on reboot for stability.

        // Save additional non-segment settings.
        if (!ledOnly) {
            persist(putBoolRobust(PROGRAM_NAMESPACE, "auto_start", autoStart), "auto_start");
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
            if (!persist(putUIntRobust(PROGRAM_NAMESPACE, WIND_STOP_KEY, static_cast<uint32_t>(windStopSeconds)), WIND_STOP_KEY)) {
                Serial.println("Failed to persist program wind-stop key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
            if (!persist(putUIntRobust(PROGRAM_NAMESPACE, H2_DELAY_KEY, static_cast<uint32_t>(h2delaySeconds)), H2_DELAY_KEY)) {
                Serial.println("Failed to persist program H2 delay key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
            if (!persist(putUIntRobust(PROGRAM_NAMESPACE, STORAGE_RUN_KEY, static_cast<uint32_t>(storageRunSeconds)), STORAGE_RUN_KEY)) {
                Serial.println("Failed to persist program storage-run key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
            if (!persist(putUIntRobust(PROGRAM_NAMESPACE, RESTART_DELAY_KEY, static_cast<uint32_t>(restartDelaySeconds)), RESTART_DELAY_KEY)) {
                Serial.println("Failed to persist program restart-delay key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
        }
        persist(putUIntRobust(PROGRAM_NAMESPACE, "total_leds", static_cast<uint32_t>(newLedCount)), "total_leds");
        if (persistFailed) {
            sendPersistFailure();
            return;
        }
        persist(putIntRobust(LED_CONFIG_NAMESPACE, BRIGHTNESS_DIVISOR_KEY, brightnessDivisor), BRIGHTNESS_DIVISOR_KEY);
        if (persistFailed) {
            sendPersistFailure();
            return;
        }
        if (!ledOnly) {
            if (!persist(putUIntRobust(LED_CONFIG_NAMESPACE, WIND_STOP_KEY, windStopSeconds), WIND_STOP_KEY)) {
                Serial.println("Failed to persist led-config wind-stop key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
            if (!persist(putUIntRobust(LED_CONFIG_NAMESPACE, H2_DELAY_KEY, h2delaySeconds), H2_DELAY_KEY)) {
                Serial.println("Failed to persist led-config H2 delay key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
            if (!persist(putUIntRobust(LED_CONFIG_NAMESPACE, STORAGE_RUN_KEY, storageRunSeconds), STORAGE_RUN_KEY)) {
                Serial.println("Failed to persist led-config storage-run key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
            if (!persist(putUIntRobust(LED_CONFIG_NAMESPACE, RESTART_DELAY_KEY, restartDelaySeconds), RESTART_DELAY_KEY)) {
                Serial.println("Failed to persist led-config restart-delay key");
            }
            if (persistFailed) {
                sendPersistFailure();
                return;
            }
        }
        persist(putIntRobust(LED_CONFIG_NAMESPACE, "led_data_pin", static_cast<int32_t>(ledDataPin)), "led_data_pin");
        if (!ledOnly) {
            persist(putIntRobust(LED_CONFIG_NAMESPACE, "wind_relay_pin", static_cast<int32_t>(windRelayPin)), "wind_relay_pin");
            persist(putIntRobust(LED_CONFIG_NAMESPACE, ELECTROLYSER_RELAY_PIN_KEY, static_cast<int32_t>(electrolyserRelayPin)), ELECTROLYSER_RELAY_PIN_KEY);
        }
        if (persistFailed) {
            sendPersistFailure();
            return;
        }

        state.totalLeds = newLedCount;
        applyLedCount(state);

        // Update directions in runtime state
        state.windDirForward = wdir;
        state.solarDirForward = sdir;
        state.hydrogenTransportDirForward = htdir;
        state.hydrogenStorage1DirForward = h1dir;
        state.hydrogenStorage2DirForward = h2dir;
        state.h2ConsumptionDirForward = hcdir;
        state.electricityTransportDirForward = etdir;
        state.storagePowerstationDirForward = spdir;
        state.fabricationDirForward = fbdir;

        // Update enabled flags in runtime state
        state.windEnabled = wen;
        state.solarEnabled = sen;
        state.electrolyserEnabled = elyen;
        state.hydrogenTransportEnabled = hten;
        state.hydrogenStorageEnabled = hsen;
        state.h2ConsumptionEnabled = hcen;
        state.fabricationEnabled = fben;
        state.electricityTransportEnabled = eten;
        state.storagePowerstationEnabled = spen;
        // Legacy segments not used in current flow: keep disabled.
        state.electricityProductionEnabled = false;
        state.storageTransportEnabled = false;
        if (!ledOnly) {
            state.autoStartEnabled = autoStart;
            if (!autoStart) {
                state.autoStartTriggered = false;
                state.buttonDisabled = false;
            }
        }

        // Update delays in runtime state
        state.windDelay = wdly;
        state.solarDelay = sdly;
        state.hydrogenTransportDelay = htdly;
        state.hydrogenStorage1Delay = h1dly;
        state.hydrogenStorage2Delay = h2dly;
        state.h2ConsumptionDelay = hcdly;
        state.electricityTransportDelay = etdly;
        state.storagePowerstationDelay = spdly;
        state.fabricationDelay = fbdly;
    if (!ledOnly) {
        state.windStopSeconds = windStopSeconds;
        state.hydrogenTransportDelaySeconds = h2delaySeconds;
        state.storageRunSeconds = storageRunSeconds;
        state.restartDelaySeconds = restartDelaySeconds;
    }

        // Update effect types in runtime
        state.windEffectType = weff;
        state.solarEffectType = seff;
        state.hydrogenTransportEffectType = hteff;
        state.hydrogenStorage1EffectType = h1eff;
        state.hydrogenStorage2EffectType = h2eff;
        state.h2ConsumptionEffectType = hceff;
        state.electricityTransportEffectType = eteff;
        state.storagePowerstationEffectType = speff;
        state.fabricationEffectType = fbeff;

        // Update colors in runtime
        auto unpackColorLocal = [](uint32_t v) -> CRGB { return CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF); };
        state.windColor = unpackColorLocal(wcol);
        state.solarColor = unpackColorLocal(scol);
        state.hydrogenTransportColor = unpackColorLocal(htcol);
        state.hydrogenStorage1Color = unpackColorLocal(hs1col);
        state.hydrogenStorage2Color = unpackColorLocal(hs2col);
        state.h2ConsumptionColor = unpackColorLocal(hccol);
        state.fabricationColor = unpackColorLocal(fbcol);
        state.electricityTransportColor = unpackColorLocal(etcol);
        state.storagePowerstationColor = unpackColorLocal(spcol);

        // Handle custom segments - use the tmpCustoms data already parsed for overlap validation
        for (int i = 0; i < SystemState::MAX_CUSTOM_SEGMENTS; ++i) {
            if (!tmpCustoms[i].present) continue;  // Use tmpCustoms instead of checking state.custom[i].inUse
            auto &t = tmpCustoms[i];
            auto &cs = state.custom[i];
            // Save prefs
            String p = String("cust") + String(i) + "_";
            if (t.name != cs.name) {
                persist(putStringRobust(LED_CONFIG_NAMESPACE, (p+"name").c_str(), t.name), (p+"name").c_str());
                if (persistFailed) break;
            }
            if (t.s != cs.start) {
                persist(putIntRobust(LED_CONFIG_NAMESPACE, (p+"s").c_str(), t.s), (p+"s").c_str());
            }
            if (persistFailed) break;
            if (t.e != cs.end) {
                persist(putIntRobust(LED_CONFIG_NAMESPACE, (p+"e").c_str(), t.e), (p+"e").c_str());
            }
            if (persistFailed) break;
            if (t.dir != cs.dirForward) {
                persist(putBoolRobust(LED_CONFIG_NAMESPACE, (p+"dir").c_str(), t.dir), (p+"dir").c_str());
            }
            if (persistFailed) break;
            if (t.en != cs.enabled) {
                persist(putBoolRobust(LED_CONFIG_NAMESPACE, (p+"en").c_str(), t.en), (p+"en").c_str());
            }
            if (persistFailed) break;
            if (t.dly != cs.delay) {
                persist(putIntRobust(LED_CONFIG_NAMESPACE, (p+"delay").c_str(), t.dly), (p+"delay").c_str());
            }
            if (persistFailed) break;
            if (t.col != packColorLocal(cs.color)) {
                persist(putUIntRobust(LED_CONFIG_NAMESPACE, (p+"color").c_str(), t.col), (p+"color").c_str());
            }
            if (persistFailed) break;
            if (t.eff != cs.effectType) {
                persist(putIntRobust(LED_CONFIG_NAMESPACE, (p+"eff").c_str(), t.eff), (p+"eff").c_str());
            }
            if (persistFailed) break;
            // Update runtime
            cs.name = t.name; 
            cs.start = t.s; 
            cs.end = t.e; 
            cs.dirForward = t.dir; 
            cs.enabled = t.en; 
            cs.delay = t.dly; 
            cs.effectType = t.eff; 
            cs.color = CRGB((t.col>>16)&0xFF,(t.col>>8)&0xFF,t.col&0xFF);
        }
        if (persistFailed) {
            sendPersistFailure();
            return;
        }

        if (request->hasParam("start_program", true)) {
            state.testMode = false;
            fill_solid(state.leds, state.totalLeds, CRGB::Black);
            FastLED.show();
            resetAllVariables();
            setRelayWind(false);
            setRelayElectrolyser(false);
            state.autoStartTriggered = true;
            state.buttonDisabled = true;
            state.generalTimerActive = true;
            timers.generalTimerStartTime = millis();
            timers.storageProgramStartTime = 0;
            timers.restartDelayStartTime = 0;
            state.restartDelayActive = false;
            state.windOn = true;
            state.solarOn = false;
            digitalWrite(state.buttonLedPin, LOW);
        }

        resetNvsRecoveryFlag();
        request->redirect("/");
    });

    server.on("/update_pins", HTTP_POST, [](AsyncWebServerRequest *request){
        auto getOutputPin = [&](const char* name, uint8_t &outPin) -> bool {
            if (!request->hasParam(name, true)) return false;
            int v = request->getParam(name, true)->value().toInt();
            if (v < 0 || v > 39) return false;
            outPin = static_cast<uint8_t>(v);
            return isSafeOutputGpio(outPin);
        };

        uint8_t windRelayPin = state.windRelayPin;
        uint8_t electrolyserRelayPin = state.electrolyserRelayPin;
        if (!getOutputPin("wind_relay_pin", windRelayPin) ||
            !getOutputPin("electrolyser_relay_pin", electrolyserRelayPin)) {
            request->send(400, "text/plain", "Missing or invalid pin settings");
            return;
        }

        bool okWind = putIntRobust(LED_CONFIG_NAMESPACE, "wind_relay_pin", static_cast<int32_t>(windRelayPin));
        bool okEly = putIntRobust(LED_CONFIG_NAMESPACE, ELECTROLYSER_RELAY_PIN_KEY, static_cast<int32_t>(electrolyserRelayPin));
        if (!(okWind && okEly)) {
            request->send(500, "text/plain", "Failed to persist pin settings");
            return;
        }

        state.windRelayPin = windRelayPin;
        state.electrolyserRelayPin = electrolyserRelayPin;

        resetNvsRecoveryFlag();
        request->send(200, "text/plain", "OK");
    });

    // Update only timing settings (lightweight path to avoid full-form validation failures)
    server.on("/update_timing", HTTP_POST, [](AsyncWebServerRequest *request){
        auto getDelaySeconds = [&](const char* name, uint16_t &outSeconds) -> bool {
            if (!request->hasParam(name, true)) return false;
            int v = request->getParam(name, true)->value().toInt();
            if (v < 0 || v > 600) return false;
            outSeconds = static_cast<uint16_t>(v);
            return true;
        };

        uint16_t windStopSeconds = state.windStopSeconds;
        uint16_t h2delaySeconds = state.hydrogenTransportDelaySeconds;
        uint16_t storageRunSeconds = state.storageRunSeconds;
        uint16_t restartDelaySeconds = state.restartDelaySeconds;
        if (!getDelaySeconds("wind_stop_s", windStopSeconds) ||
            !getDelaySeconds("h2_trans_delay_s", h2delaySeconds) ||
            !getDelaySeconds("storage_run_s", storageRunSeconds) ||
            !getDelaySeconds("restart_delay_s", restartDelaySeconds)) {
            request->send(400, "text/plain", "Missing or invalid timing parameters");
            return;
        }

        state.windStopSeconds = windStopSeconds;
        state.hydrogenTransportDelaySeconds = h2delaySeconds;
        state.storageRunSeconds = storageRunSeconds;
        state.restartDelaySeconds = restartDelaySeconds;
        String timingFailedKey;
        auto markTiming = [&](bool ok, const char* key) {
            if (!ok && timingFailedKey.length() == 0) {
                timingFailedKey = key;
            }
            return ok;
        };
        bool okProgramWind = markTiming(putUIntRobust(PROGRAM_NAMESPACE, WIND_STOP_KEY, static_cast<uint32_t>(windStopSeconds)), WIND_STOP_KEY);
        bool okProgramH2 = markTiming(putUIntRobust(PROGRAM_NAMESPACE, H2_DELAY_KEY, static_cast<uint32_t>(h2delaySeconds)), H2_DELAY_KEY);
        bool okProgramStorage = markTiming(putUIntRobust(PROGRAM_NAMESPACE, STORAGE_RUN_KEY, static_cast<uint32_t>(storageRunSeconds)), STORAGE_RUN_KEY);
        bool okProgramRestartDelay = markTiming(putUIntRobust(PROGRAM_NAMESPACE, RESTART_DELAY_KEY, static_cast<uint32_t>(restartDelaySeconds)), RESTART_DELAY_KEY);
        bool okLedWind = markTiming(putUIntRobust(LED_CONFIG_NAMESPACE, WIND_STOP_KEY, static_cast<uint32_t>(windStopSeconds)), WIND_STOP_KEY);
        bool okLedH2 = markTiming(putUIntRobust(LED_CONFIG_NAMESPACE, H2_DELAY_KEY, static_cast<uint32_t>(h2delaySeconds)), H2_DELAY_KEY);
        bool okLedStorage = markTiming(putUIntRobust(LED_CONFIG_NAMESPACE, STORAGE_RUN_KEY, static_cast<uint32_t>(storageRunSeconds)), STORAGE_RUN_KEY);
        bool okLedRestartDelay = markTiming(putUIntRobust(LED_CONFIG_NAMESPACE, RESTART_DELAY_KEY, static_cast<uint32_t>(restartDelaySeconds)), RESTART_DELAY_KEY);

        if (!(okProgramWind && okProgramH2 && okProgramStorage && okProgramRestartDelay && okLedWind && okLedH2 && okLedStorage && okLedRestartDelay)) {
            String msg = String("Failed to persist timing settings at key: ") + (timingFailedKey.length() ? timingFailedKey : String("unknown"));
            request->send(500, "text/plain", msg);
            return;
        }

        resetNvsRecoveryFlag();
        request->send(200, "text/plain", "OK");
    });

    // Program-tab test start: starts a full-strip test without request parameters.
    server.on("/start_test_all", HTTP_POST, [](AsyncWebServerRequest *request){
        int end = state.totalLeds > 0 ? state.totalLeds - 1 : 0;

        state.testMode = true;
        state.testSegmentStart = 0;
        state.testSegmentEnd = end;
        state.testSegmentIndex = -1; // force test re-initialize
        state.testDirForward = true;
        state.testEffectType = 0;
        state.testColor = CRGB::White;
        state.testDelay = 40;
        state.testPhase = 0;
        state.testPhaseStartTime = millis();

        fill_solid(state.leds, state.totalLeds, CRGB::Black);
        FastLED.show();

        request->send(200, "text/plain", "OK");
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

        if (state.totalLeds == 0 || start < 0 || start >= state.totalLeds || end < 0 || end >= state.totalLeds || start > end) {
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
        fill_solid(state.leds, state.totalLeds, CRGB::Black);
        FastLED.show();

        request->redirect("/");
    });

    // Relay control handler - supports manual ON/OFF and return to automatic relay logic.
    server.on("/set_relay", HTTP_POST, [](AsyncWebServerRequest *request){
        if (!request->hasParam("relay", true) || !request->hasParam("state", true)) {
            request->send(400, "text/plain", "Missing parameters");
            return;
        }

        String relay = request->getParam("relay", true)->value();
        String stateVal = request->getParam("state", true)->value();

        if (relay == "auto") {
            state.relayManualMode = false;
            request->send(200, "text/plain", "Auto relay mode enabled");
            return;
        }

        if (!request->hasParam("pin", true)) {
            request->send(400, "text/plain", "Missing pin");
            return;
        }
        if (relay != "wind" && relay != "electrolyser") {
            request->send(400, "text/plain", "Invalid relay");
            return;
        }
        if (stateVal != "on" && stateVal != "off") {
            request->send(400, "text/plain", "Invalid state");
            return;
        }

        int pinInt = request->getParam("pin", true)->value().toInt();
        if (pinInt < 0 || pinInt > 39) {
            request->send(400, "text/plain", "Invalid pin");
            return;
        }
        uint8_t pin = static_cast<uint8_t>(pinInt);
        if (!isSafeOutputGpio(pin)) {
            request->send(400, "text/plain", "Pin is not safe for relay output");
            return;
        }

        bool on = (stateVal == "on");
        state.relayManualMode = true;
        if (relay == "wind") {
            state.manualWindRelayOn = on;
            setRelayWind(on);
        } else {
            state.manualElectrolyserRelayOn = on;
            setRelayElectrolyser(on);
        }

        request->send(200, "text/plain", "Relay state updated");
    });

    // Start the process chain directly from the web UI
    server.on("/start_chain", HTTP_POST, [](AsyncWebServerRequest *request){
        state.testMode = false;
        fill_solid(state.leds, state.totalLeds, CRGB::Black);
        FastLED.show();
        resetAllVariables();
        setRelayWind(false);
        setRelayElectrolyser(false);
        state.autoStartTriggered = true;
        state.buttonDisabled = true;
        state.generalTimerActive = true;
        timers.generalTimerStartTime = millis();
        timers.storageProgramStartTime = 0;
        timers.restartDelayStartTime = 0;
        state.restartDelayActive = false;
        state.windOn = true;
        digitalWrite(state.buttonLedPin, LOW);
        request->redirect("/");
    });

    // Explicit "Start programma" action (same behavior as start_chain)
    server.on("/start_program", HTTP_POST, [](AsyncWebServerRequest *request){
        state.testMode = false;
        fill_solid(state.leds, state.totalLeds, CRGB::Black);
        FastLED.show();
        resetAllVariables();
        setRelayWind(false);
        setRelayElectrolyser(false);
        state.autoStartTriggered = true;
        state.buttonDisabled = true;
        state.generalTimerActive = true;
        timers.generalTimerStartTime = millis();
        timers.storageProgramStartTime = 0;
        timers.restartDelayStartTime = 0;
        state.restartDelayActive = false;
        state.windOn = true;
        state.solarOn = false;
        digitalWrite(state.buttonLedPin, LOW);
        request->redirect("/");
    });

    // Stop test handler
    server.on("/stoptest", HTTP_POST, [](AsyncWebServerRequest *request){
        state.testMode = false;
        // Clear LEDs on exit from test for a clean state
        fill_solid(state.leds, state.totalLeds, CRGB::Black);
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

    applyLedCount(state);
}
