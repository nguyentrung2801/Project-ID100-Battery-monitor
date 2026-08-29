#pragma once

#include <Arduino.h>

void telemetryBegin();
bool telemetryBuildBootstrapPayload(String &payload);
bool telemetryCaptureAndStore(
    uint8_t channelMask,
    bool isBackfill,
    uint16_t aggregationIntervalSeconds);
bool telemetryPeekOldest(String &path, String &payload);
void telemetryAcknowledge(const String &path);
void telemetryMarkQueuedAsBackfill();
uint16_t telemetryCompactBackfillToHourly();
void telemetryDiscardDevice(const String &deviceId);
uint16_t telemetryQueueCount();
