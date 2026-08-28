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
void startAccessPoint()
{
    const IPAddress accessPointIp(4, 4, 4, 4);
    const IPAddress subnetMask(255, 255, 255, 0);

    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.softAPConfig(accessPointIp, accessPointIp, subnetMask);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
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
    samplingLoop();
    handleAPWebClient();
    mqttLoop();
    delay(2);
}
