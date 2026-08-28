#pragma once

#include <Arduino.h>

void telemetryBegin();
bool telemetryCaptureAndStore(
    uint8_t channelMask,
    bool isBackfill,
    uint16_t aggregationIntervalSeconds);
bool telemetryCaptureInitialAndStore(bool isBackfill);
bool telemetryPeekOldest(String &path, String &payload);
void telemetryAcknowledge(const String &path);
void telemetryMarkQueuedAsBackfill();
uint16_t telemetryCompactBackfillToHourly();
void telemetryDiscardDevice(const String &deviceId);
uint16_t telemetryQueueCount();
