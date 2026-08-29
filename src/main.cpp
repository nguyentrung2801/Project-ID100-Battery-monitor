#include <Arduino.h>
#include <WiFi.h>

#include "ap_web_manager.h"
#include "battery_manager.h"
#include "command_manager.h"
#include "config.h"
#include "identity_manager.h"
#include "mqtt_manager.h"
#include "sampling_manager.h"
#include "settings_manager.h"
#include "telemetry_manager.h"
#include "time_manager.h"

namespace
{
const IPAddress accessPointIp(4, 4, 4, 4);
const IPAddress subnetMask(255, 255, 255, 0);
unsigned long lastAccessPointCheckMillis = 0;

void startAccessPoint()
{
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    // AP stability has priority. STA reconnects are scheduled explicitly and
    // never while a phone is connected to the configuration AP.
    WiFi.setAutoReconnect(false);
    WiFi.softAPConfig(accessPointIp, accessPointIp, subnetMask);
    const bool started = WiFi.softAP(
        AP_SSID,
        AP_PASSWORD,
        AP_CHANNEL,
        false,
        AP_MAX_CONNECTIONS);
    Serial.printf(
        "[WiFi] AP %s, IP=%s\n",
        started ? "started" : "start failed",
        WiFi.softAPIP().toString().c_str());
}

void keepAccessPointAlive()
{
    const unsigned long now = millis();
    if (now - lastAccessPointCheckMillis < AP_HEALTH_CHECK_MS)
    {
        return;
    }
    lastAccessPointCheckMillis = now;

    const wifi_mode_t mode = WiFi.getMode();
    const bool apModeEnabled = mode == WIFI_AP || mode == WIFI_AP_STA;
    if (apModeEnabled && WiFi.softAPIP() == accessPointIp)
    {
        return;
    }

    Serial.println("[WiFi] AP health check failed, restarting AP");
    WiFi.softAPdisconnect(false);
    startAccessPoint();
}

} // namespace

void setup()
{
    Serial.begin(115200);
    delay(300);

    settingsBegin();
    identityBegin();
    commandBegin();
    telemetryBegin();
    batteryBegin();
    samplingBegin();
    startAccessPoint();
    connectSavedWifi();
    setupTime();
    setupAPWebServer();
    mqttBegin();

    Serial.printf(
        "[BOOT] %s %s - http://4.4.4.4\n",
        DEVICE_NAME,
        FIRMWARE_VERSION);
}

void loop()
{
    keepAccessPointAlive();
    maintainSavedWifi();
    samplingLoop();
    handleAPWebClient();
    mqttLoop();
    delay(2);
}
