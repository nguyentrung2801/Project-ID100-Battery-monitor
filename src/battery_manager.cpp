#include "battery_manager.h"

#include "config.h"
#include "identity_manager.h"
#include "settings_manager.h"
#include "time_manager.h"

#include <ArduinoJson.h>
#include <WiFi.h>

namespace
{
const uint8_t BATTERY_PINS[ADC_CHANNEL_COUNT] = {
    PIN_BATTERY_1,
    PIN_BATTERY_2,
    PIN_BATTERY_3,
    PIN_BATTERY_4,
};

BatteryReading readings[ADC_CHANNEL_COUNT];
uint32_t measurementTimestamp = 0;
unsigned long measurementMillis = 0;

uint16_t adcToBatteryVoltageMv(uint8_t channel, uint16_t adcVoltageMv)
{
    (void)channel;
    const double calibratedMv =
        static_cast<double>(adcVoltageMv) * ADC_CAL_SLOPE + ADC_CAL_OFFSET_MV;
    return calibratedMv > 0.0 ? static_cast<uint16_t>(lround(calibratedMv)) : 0;
}

float voltageToPercent(uint16_t batteryVoltageMv)
{
    const float voltageRange = BATTERY_FULL_MV - BATTERY_EMPTY_MV;
    const float percent = 100.0F *
                          (static_cast<float>(batteryVoltageMv) - BATTERY_EMPTY_MV) /
                          voltageRange;

    return constrain(percent, 0.0F, 100.0F);
}

String batteryStatus(uint16_t batteryVoltageMv)
{
    if (batteryVoltageMv < BATTERY_DISCONNECTED_MV)
    {
        return "not_connected";
    }

    if (batteryVoltageMv <= BATTERY_EMPTY_MV)
    {
        return "empty";
    }

    if (batteryVoltageMv < BATTERY_LOW_MV)
    {
        return "low";
    }

    if (batteryVoltageMv > BATTERY_OVER_VOLTAGE_MV)
    {
        return "over_voltage";
    }

    if (batteryVoltageMv >= BATTERY_FULL_MV)
    {
        return "full";
    }

    return "normal";
}

void addBatteryToJson(JsonArray &batteries, const BatteryReading &reading)
{
    JsonObject battery = batteries.add<JsonObject>();
    battery["device_id"] = reading.deviceId;
    battery["device_name"] = reading.deviceName;
    battery["gpio"] = reading.pin;
    battery["adc_raw"] = reading.adcRaw;
    battery["adc_voltage_mv"] = reading.adcVoltageMv;
    battery["battery_voltage_mv"] = reading.batteryVoltageMv;
    battery["battery_percent"] = serialized(String(reading.percent, 1));
    battery["status"] = reading.status;
}
} // namespace

void batteryBegin()
{
    analogReadResolution(12);

    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        pinMode(BATTERY_PINS[channel], INPUT);
        analogSetPinAttenuation(BATTERY_PINS[channel], ADC_11db);

        BatteryReading &reading = readings[channel];
        reading.pin = BATTERY_PINS[channel];
        // Use the same MAC-derived ID for telemetry, Web AP and command validation.
        // Keeping a second hard-coded ID here causes the server to address one
        // device while command_manager validates against another.
        reading.deviceId = getDeviceId(channel);
        char deviceName[20];
        snprintf(
            deviceName,
            sizeof(deviceName),
            "ID-100 Unit %02u",
            channel + 1);
        reading.deviceName = deviceName;
    }
}

void batterySampleAll()
{
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        batterySampleChannel(channel);
    }

    measurementTimestamp = getUnixTimestamp();
    measurementMillis = millis();
}

void batterySampleChannel(uint8_t channel)
{
    if (channel >= ADC_CHANNEL_COUNT)
    {
        return;
    }

    uint32_t rawTotal = 0;
    uint32_t millivoltTotal = 0;

    for (uint8_t sample = 0; sample < ADC_SAMPLES; sample++)
    {
        rawTotal += analogRead(BATTERY_PINS[channel]);
        millivoltTotal += analogReadMilliVolts(BATTERY_PINS[channel]);
        delayMicroseconds(ADC_SAMPLE_DELAY_US);
    }

    BatteryReading &reading = readings[channel];
    reading.adcRaw = rawTotal / ADC_SAMPLES;
    reading.adcVoltageMv = millivoltTotal / ADC_SAMPLES;
    reading.batteryVoltageMv = adcToBatteryVoltageMv(
        channel,
        reading.adcVoltageMv);
    reading.percent = voltageToPercent(reading.batteryVoltageMv);
    reading.status = batteryStatus(reading.batteryVoltageMv);
    reading.capturedAt = getUtcIsoTimestamp();
    reading.wifiRssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    reading.uptimeSeconds = millis() / 1000;

    measurementTimestamp = getUnixTimestamp();
    measurementMillis = millis();
}

void batteryApplyAveragedSample(
    uint8_t channel,
    uint16_t adcRaw,
    uint16_t adcVoltageMv)
{
    if (channel >= ADC_CHANNEL_COUNT)
    {
        return;
    }

    BatteryReading &reading = readings[channel];
    reading.adcRaw = adcRaw;
    reading.adcVoltageMv = adcVoltageMv;
    reading.batteryVoltageMv = adcToBatteryVoltageMv(channel, adcVoltageMv);
    reading.percent = voltageToPercent(reading.batteryVoltageMv);
    reading.status = batteryStatus(reading.batteryVoltageMv);
    reading.capturedAt = getUtcIsoTimestamp();
    reading.wifiRssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    reading.uptimeSeconds = millis() / 1000;

    measurementTimestamp = getUnixTimestamp();
    measurementMillis = millis();
}

const BatteryReading &batteryGet(uint8_t index)
{
    return readings[index < ADC_CHANNEL_COUNT ? index : 0];
}

String batteryDashboardJson()
{
    const AppSettings &settings = settingsGet();
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;

    JsonDocument document;
    document["schema_version"] = SCHEMA_VERSION;
    document["monitor_name"] = DEVICE_NAME;
    document["tester_name"] = settings.testerName;
    document["test_location"] = settings.testLocation;
    document["wifi_ssid"] = wifiConnected ? WiFi.SSID() : "";
    document["wifi_ip"] = wifiConnected ? WiFi.localIP().toString() : "0.0.0.0";
    document["wifi_rssi"] = wifiConnected ? WiFi.RSSI() : 0;
    document["uptime_s"] = millis() / 1000;
    document["firmware_version"] = FIRMWARE_VERSION;
    document["timestamp"] = measurementTimestamp;
    document["measurement_interval_s"] =
        SAMPLES_PER_MINUTE * MINUTE_AVERAGES_PER_TELEMETRY;
    document["measurement_age_s"] = (millis() - measurementMillis) / 1000;
    document["esp_status"] = wifiConnected ? "online" : "offline";

    JsonArray batteries = document["batteries"].to<JsonArray>();
    for (uint8_t index = 0; index < ADC_CHANNEL_COUNT; index++)
    {
        addBatteryToJson(batteries, readings[index]);
    }

    String payload;
    serializeJson(document, payload);
    return payload;
}
