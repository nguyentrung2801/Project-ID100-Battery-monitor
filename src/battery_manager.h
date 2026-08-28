#pragma once

#include <Arduino.h>

struct BatteryReading
{
    uint8_t pin;
    String deviceId;
    String deviceName;
    uint16_t adcRaw;
    uint16_t adcVoltageMv;
    uint16_t batteryVoltageMv;
    float percent;
    String status;
    String capturedAt;
    int32_t wifiRssi;
    uint32_t uptimeSeconds;
};

void batteryBegin();
void batterySampleAll();
void batterySampleChannel(uint8_t channel);
void batteryApplyAveragedSample(
    uint8_t channel,
    uint16_t adcRaw,
    uint16_t adcVoltageMv);
const BatteryReading &batteryGet(uint8_t index);
String batteryDashboardJson();
