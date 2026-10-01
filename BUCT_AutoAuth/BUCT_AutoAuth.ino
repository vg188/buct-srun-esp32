// BUCT Srun ESP32-C3: Arduino adapter only; recovery policy lives in auth_controller.
// GPIO8: 3s heartbeat=online, 1Hz=WiFi, 4Hz=checking, 8Hz=credential error.
// Board: ESP32C3 Dev Module, USB CDC On Boot=Enabled (native USB Serial/JTAG).
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <stdarg.h>
#include "Config.h"
#include "srun_auth.h"
#include "auth_controller.h"

#if defined(ARDUINO_ARCH_ESP32) && (!defined(ARDUINO_USB_CDC_ON_BOOT) || ARDUINO_USB_CDC_ON_BOOT != 1)
#error "This board uses native USB: set Tools -> USB CDC On Boot -> Enabled, then upload again."
#endif
#if defined(ARDUINO_ARCH_ESP32) && (!defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE != 1)
#error "This sketch requires ESP32-C3 hardware USB Serial/JTAG mode."
#endif

static const char* FIRMWARE_ID = "buct-recovery-2026.10.01";
struct Diagnostics {
    uint32_t wifiStarts = 0, probes = 0, portalQueries = 0, logins = 0, logouts = 0;
    int lastProbe = -1;  // -1=not run, 0=failed, 1=204
    int lastPortal = -1; // -1=not run, -2=unknown, 0=offline, 1=online
    int lastAuth = -1;   // -1=not run, otherwise srun::AuthResult
};
static Diagnostics diagnostics;

#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

static void logState(const char* message) {
    // Standalone USB power must not wait for a serial monitor or flush USB output.
    if (Serial) Serial.printf("[%8lus] %s\n", (unsigned long)(millis() / 1000), message);
}

void srunDebugLog(const char* fmt, ...) {
    if (!Serial) return;
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    logState(buffer);
}

class ArduinoPort : public buct::Port {
public:
    uint32_t now() const override { return millis(); }
    buct::Link link() const override {
        if (WiFi.status() == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0 &&
            (uint32_t)WiFi.gatewayIP() != 0) return buct::Link::READY;
        // WL_CONNECTED alone cannot distinguish association from DHCP in all
        // Arduino core versions; ask the ESP-IDF station for its associated AP.
        wifi_ap_record_t ap{};
        return esp_wifi_sta_get_ap_info(&ap) == ESP_OK
            ? buct::Link::WAIT_ADDRESS : buct::Link::DOWN;
    }
    void beginWiFi() override {
        ++diagnostics.wifiStarts;
        wl_status_t result = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        srunDebugLog("WiFi.begin status=%d", (int)result);
    }
    void disconnectWiFi() override {
        bool ok = WiFi.disconnect(false, false); // keep radio/config; no flash erase
        srunDebugLog("WiFi.disconnect accepted=%d", (int)ok);
    }
    bool probe(uint32_t timeout) override {
        ++diagnostics.probes;
        bool ok = srun::probeOnline(timeout);
        diagnostics.lastProbe = ok ? 1 : 0;
        return ok;
    }
    bool onlineInfo(srun::OnlineInfo& info) override {
        ++diagnostics.portalQueries;
        bool known = srun::getOnlineInfo(info);
        diagnostics.lastPortal = known ? (info.online ? 1 : 0) : -2;
        return known;
    }
    srun::AuthResult login() override {
        ++diagnostics.logins;
        srun::AuthResult result = srun::login(CAMPUS_USERNAME, CAMPUS_PASSWORD, SRUN_AC_ID);
        diagnostics.lastAuth = (int)result;
        return result;
    }
    bool logout() override {
        ++diagnostics.logouts;
        return srun::logout();
    }
    void log(const char* message) override { logState(message); }
};

static constexpr uint32_t BACKOFF[] = AUTH_RETRY_BACKOFF_S;
static_assert(sizeof(BACKOFF) / sizeof(BACKOFF[0]) > 0, "Backoff cannot be empty");
// Validate values before converting seconds to milliseconds. Conservative half-
// range bound also keeps user-edited intervals sensible for rollover scheduling.
constexpr bool validSeconds(uint32_t s) { return s > 0 && s <= 2147483UL; }
constexpr bool validBackoff(size_t i) {
    return i == sizeof(BACKOFF) / sizeof(BACKOFF[0]) ? true
        : validSeconds(BACKOFF[i]) && validBackoff(i + 1);
}
static_assert(validBackoff(0), "Invalid authentication backoff");
static_assert(validSeconds(WIFI_RETRY_INTERVAL_S) && validSeconds(WIFI_DHCP_TIMEOUT_S), "Invalid WiFi timeout");
static_assert(validSeconds(WIFI_SETTLE_S) && validSeconds(PROBE_INTERVAL_S), "Invalid check interval");
static_assert(PORTAL_VERIFY_INTERVAL_S == 0 || validSeconds(PORTAL_VERIFY_INTERVAL_S), "Invalid portal interval");
static_assert(validSeconds(ACCOUNT_REPLACEMENT_COOLDOWN_S), "Invalid replacement cooldown");
static_assert(PROBE_TIMEOUT_MS > 0 && PROBE_TIMEOUT_MS <= 2147483647UL, "Invalid probe timeout");
static_assert(ACCOUNT_REPLACEMENT_ENABLED == 0 || ACCOUNT_REPLACEMENT_ENABLED == 1, "Replacement must be 0 or 1");

static ArduinoPort port;
static const buct::Settings settings = {
    WIFI_RETRY_INTERVAL_S * 1000UL, WIFI_DHCP_TIMEOUT_S * 1000UL,
    WIFI_SETTLE_S * 1000UL, PROBE_INTERVAL_S * 1000UL, PROBE_TIMEOUT_MS,
    PORTAL_VERIFY_INTERVAL_S * 1000UL, ACCOUNT_REPLACEMENT_ENABLED != 0,
    ACCOUNT_REPLACEMENT_COOLDOWN_S * 1000UL,
    BACKOFF, sizeof(BACKOFF) / sizeof(BACKOFF[0]), CAMPUS_USERNAME
};
static buct::Controller controller(port, settings);

static const char* stateName() {
    switch (controller.state()) {
    case buct::State::WIFI_CONNECTING: return "WIFI_CONNECTING";
    case buct::State::ROUTER_SETTLING: return "ROUTER_SETTLING";
    case buct::State::CHECKING: return "CHECKING";
    case buct::State::VERIFY_LOGIN: return "VERIFY_LOGIN";
    case buct::State::ONLINE: return "ONLINE";
    case buct::State::FATAL_CREDENTIALS: return "FATAL_CREDENTIALS";
    }
    return "UNKNOWN";
}

// Observability only: reads local status/counters, never sends any network request.
// A monitor opened after boot gets a snapshot; offline USB power still never waits.
static void diagnosticsTick() {
    static bool wasAttached = false;
    static uint32_t last = 0;
    bool attached = (bool)Serial;
    if (!attached) { wasAttached = false; return; }
    if (wasAttached && uint32_t(millis() - last) < 15000) return;
    if (!wasAttached) {
        srunDebugLog("MONITOR attached; firmware=%s; replacement=%d", FIRMWARE_ID,
                     (int)ACCOUNT_REPLACEMENT_ENABLED);
    }
    wasAttached = true;
    last = millis();
    buct::Link link = port.link();
    const char* linkText = link == buct::Link::READY ? "READY" :
                          link == buct::Link::WAIT_ADDRESS ? "WAIT_ADDRESS" : "DOWN";
    srunDebugLog("STATUS state=%s wifi=%d link=%s", stateName(), (int)WiFi.status(), linkText);
    srunDebugLog("LAN ip=%s gateway=%s dns=%s", WiFi.localIP().toString().c_str(),
                 WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str());
    srunDebugLog("COUNTERS wifiStart=%lu probe=%lu(last=%d) portal=%lu(last=%d) login=%lu(last=%d) logout=%lu",
        (unsigned long)diagnostics.wifiStarts, (unsigned long)diagnostics.probes, diagnostics.lastProbe,
        (unsigned long)diagnostics.portalQueries, diagnostics.lastPortal,
        (unsigned long)diagnostics.logins, diagnostics.lastAuth, (unsigned long)diagnostics.logouts);
}

static void ledTick() {
    static uint32_t last = 0;
    if (millis() - last < 25) return;
    last = millis();
    uint32_t onMs = 125, offMs = 125;
    switch (controller.state()) {
    case buct::State::ONLINE: onMs = 60; offMs = 2940; break;
    case buct::State::WIFI_CONNECTING: onMs = 500; offMs = 500; break;
    case buct::State::FATAL_CREDENTIALS: onMs = 60; offMs = 60; break;
    default: break;
    }
    digitalWrite(LED_BUILTIN, (millis() % (onMs + offMs)) < onMs);
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, false);
    logState(FIRMWARE_ID);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    // Single owner: the controller retries indefinitely; don't race SDK retry
    // callbacks against an application disconnect/begin sequence.
    WiFi.setAutoReconnect(false);
    WiFi.setSleep(true); // Preserve the proven low-power setting.
    controller.begin();
}

void loop() {
    ledTick();
    diagnosticsTick();
    controller.tick();
    diagnosticsTick();
    delay(10);
}
