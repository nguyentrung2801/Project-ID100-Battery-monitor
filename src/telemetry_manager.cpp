#include "telemetry_manager.h"

#include "battery_manager.h"
#include "command_manager.h"
#include "config.h"
#include "identity_manager.h"
#include "settings_manager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>

#include <algorithm>
#include <vector>

namespace
{
Preferences queuePreferences;
bool storageReady = false;

bool isTelemetryFile(const String &name)
{
    return name.indexOf("tele_") >= 0 && name.endsWith(".json");
}

String storagePath(const String &name)
{
    return name.startsWith("/") ? name : "/" + name;
}

String nextFilePath()
{
    const uint32_t id = queuePreferences.getUInt("file_id", 0) + 1;
    queuePreferences.putUInt("file_id", id);

    char path[24];
    snprintf(path, sizeof(path), "/tele_%010lu.json", id);
    return String(path);
}

bool readDocument(const String &path, JsonDocument &document)
{
    File file = LittleFS.open(path, "r");
    if (!file)
    {
        return false;
    }

    const DeserializationError error = deserializeJson(document, file);
    file.close();
    return !error;
}

bool writeDocument(const String &path, const JsonDocument &document)
{
    File file = LittleFS.open(path, "w");
    if (!file)
    {
        return false;
    }

    const bool written = serializeJson(document, file) > 0;
    file.close();
    return written;
}

void addMeasurement(JsonArray &measurements, uint8_t channel, bool isBackfill)
{
    const BatteryReading &reading = batteryGet(channel);
    JsonObject measurement = measurements.add<JsonObject>();
    measurement["device_id"] = reading.deviceId;
    measurement["channel"] = channel + 1;
    measurement["sequence_no"] = commandNextSequence(channel);
    measurement["captured_at"] = reading.capturedAt;
    measurement["adc_raw"] = reading.adcRaw;
    measurement["adc_voltage_mv"] = reading.adcVoltageMv;
    measurement["battery_voltage_mv"] = reading.batteryVoltageMv;
    measurement["battery_percent"] = serialized(String(reading.percent, 1));
    measurement["wifi_rssi"] = reading.wifiRssi;
    measurement["uptime_s"] = reading.uptimeSeconds;
    measurement["is_backfill"] = isBackfill;
}

void enforceQueueLimit()
{
    while (telemetryQueueCount() > TELEMETRY_QUEUE_LIMIT)
    {
        String path;
        String payload;
        if (!telemetryPeekOldest(path, payload))
        {
            return;
        }
        LittleFS.remove(path);
    }
}
} // namespace

void telemetryBegin()
{
    storageReady = LittleFS.begin(true);
    queuePreferences.begin("telemetry", false);
}

bool telemetryBuildBootstrapPayload(String &payload)
{
    JsonDocument document;
    document["schema_version"] = SCHEMA_VERSION;
    document["type"] = "telemetry_batch";
    document["gateway_id"] = getGatewayId();
    document["gateway_name"] = settingsGet().gatewayName;
    document["firmware_version"] = FIRMWARE_VERSION;
    document["boot_id"] = getBootId();
    document["tester_name"] = settingsGet().testerName;
    document["test_location"] = settingsGet().testLocation;

    JsonArray measurements = document["measurements"].to<JsonArray>();
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        addMeasurement(measurements, channel, false);
    }

    payload = "";
    serializeJson(document, payload);
    return measurements.size() == ADC_CHANNEL_COUNT && !payload.isEmpty();
}

bool captureAndStore(
    uint8_t channelMask,
    bool isBackfill,
    bool includeIdle,
    uint16_t aggregationIntervalSeconds)
{
    JsonDocument document;
    document["schema_version"] = SCHEMA_VERSION;
    document["type"] = "telemetry_batch";
    document["gateway_id"] = getGatewayId();
    document["gateway_name"] = settingsGet().gatewayName;
    document["firmware_version"] = FIRMWARE_VERSION;
    document["boot_id"] = getBootId();
    document["tester_name"] = settingsGet().testerName;
    document["test_location"] = settingsGet().testLocation;
    // Internal queue metadata. It is removed before MQTT publishing so the
    // server-facing telemetry schema remains unchanged.
    document["_aggregation_interval_s"] = aggregationIntervalSeconds;

    JsonArray measurements = document["measurements"].to<JsonArray>();
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        const bool channelSelected = (channelMask & (1U << channel)) != 0;
        if (!channelSelected ||
            (!includeIdle && commandChannelState(channel) != ChannelState::Running))
        {
            continue;
        }

        addMeasurement(measurements, channel, isBackfill);
    }

    if (measurements.isNull() || measurements.size() == 0 || !storageReady)
    {
        return false;
    }

    const bool stored = writeDocument(nextFilePath(), document);
    if (stored)
    {
        enforceQueueLimit();
    }
    return stored;
}

bool telemetryCaptureAndStore(
    uint8_t channelMask,
    bool isBackfill,
    uint16_t aggregationIntervalSeconds)
{
    return captureAndStore(
        channelMask,
        isBackfill,
        false,
        aggregationIntervalSeconds);
}

bool telemetryPeekOldest(String &path, String &payload)
{
    if (!storageReady)
    {
        return false;
    }

    File root = LittleFS.open("/");
    File file = root.openNextFile();
    String oldestPath;

    while (file)
    {
        const String name = file.name();
        if (!file.isDirectory() && isTelemetryFile(name) &&
            (oldestPath.isEmpty() || storagePath(name) < oldestPath))
        {
            oldestPath = storagePath(name);
        }
        file = root.openNextFile();
    }
    root.close();

    if (oldestPath.isEmpty())
    {
        return false;
    }

    JsonDocument document;
    if (!readDocument(oldestPath, document))
    {
        return false;
    }

    // Never expose queue/compaction metadata to the server. The non-prefixed
    // names are also removed for compatibility with files produced by an
    // earlier development build.
    document.remove("_aggregation_interval_s");
    document.remove("_samples_aggregated");
    document.remove("aggregation_interval_s");
    document.remove("samples_aggregated");
    document.remove("label");
    payload = "";
    serializeJson(document, payload);
    path = oldestPath;
    return true;
}

void telemetryAcknowledge(const String &path)
{
    if (!path.isEmpty())
    {
        LittleFS.remove(path);
    }
}

void telemetryMarkQueuedAsBackfill()
{
    if (!storageReady)
    {
        return;
    }

    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        const String path = storagePath(file.name());
        file.close();

        if (isTelemetryFile(path))
        {
            JsonDocument document;
            if (readDocument(path, document))
            {
                for (JsonObject measurement : document["measurements"].as<JsonArray>())
                {
                    measurement["is_backfill"] = true;
                }
                writeDocument(path, document);
            }
        }
        file = root.openNextFile();
    }
    root.close();
}

uint16_t telemetryCompactBackfillToHourly()
{
    if (!storageReady)
    {
        return 0;
    }

    struct BackfillCandidate
    {
        String path;
        uint8_t channelMask;
    };
    std::vector<BackfillCandidate> candidates;
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        const String path = storagePath(file.name());
        const bool candidate = !file.isDirectory() && isTelemetryFile(path);
        file.close();

        if (candidate)
        {
            JsonDocument document;
            if (readDocument(path, document) &&
                (document["_aggregation_interval_s"].as<uint16_t>() ==
                     SAMPLES_PER_MINUTE * MINUTE_AVERAGES_PER_TELEMETRY ||
                 document["aggregation_interval_s"].as<uint16_t>() ==
                     SAMPLES_PER_MINUTE * MINUTE_AVERAGES_PER_TELEMETRY))
            {
                bool allBackfill = true;
                uint8_t channelMask = 0;
                JsonArray measurements = document["measurements"].as<JsonArray>();
                for (JsonObject measurement : measurements)
                {
                    if (!measurement["is_backfill"].as<bool>())
                    {
                        allBackfill = false;
                        break;
                    }
                    const int channelNumber = measurement["channel"] | 0;
                    if (channelNumber < 1 || channelNumber > ADC_CHANNEL_COUNT)
                    {
                        allBackfill = false;
                        break;
                    }
                    channelMask |= 1U << (channelNumber - 1);
                }
                if (allBackfill && channelMask != 0)
                {
                    candidates.push_back({path, channelMask});
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const BackfillCandidate &left, const BackfillCandidate &right)
        { return left.path < right.path; });

    // A channel can start at a different time from the other channel. Group
    // batches with the same channel mask so single-channel batches can
    // still be compacted correctly even when their files are interleaved.
    std::vector<String> tenMinutePaths;
    for (uint8_t mask = 1; mask < (1U << ADC_CHANNEL_COUNT); mask++)
    {
        std::vector<String> matchingPaths;
        for (const BackfillCandidate &candidate : candidates)
        {
            if (candidate.channelMask == mask)
            {
                matchingPaths.push_back(candidate.path);
            }
        }

        const size_t completeCount =
            (matchingPaths.size() / TEN_MINUTE_BATCHES_PER_HOUR) *
            TEN_MINUTE_BATCHES_PER_HOUR;
        tenMinutePaths.insert(
            tenMinutePaths.end(),
            matchingPaths.begin(),
            matchingPaths.begin() + completeCount);
    }

    struct Aggregate
    {
        bool active = false;
        String deviceId;
        uint8_t channel = 0;
        uint32_t sequenceNo = 0;
        String capturedAt;
        uint64_t adcRaw = 0;
        uint64_t adcVoltageMv = 0;
        uint64_t batteryVoltageMv = 0;
        double batteryPercent = 0;
        int64_t wifiRssi = 0;
        uint32_t uptimeSeconds = 0;
        uint8_t count = 0;
    };

    uint16_t compactedHours = 0;
    for (size_t start = 0;
         start + TEN_MINUTE_BATCHES_PER_HOUR <= tenMinutePaths.size();
         start += TEN_MINUTE_BATCHES_PER_HOUR)
    {
        Aggregate aggregates[ADC_CHANNEL_COUNT];
        uint8_t expectedChannelMask = 0;
        bool groupValid = true;
        JsonDocument hourlyDocument;

        for (size_t offset = 0; offset < TEN_MINUTE_BATCHES_PER_HOUR; offset++)
        {
            JsonDocument source;
            if (!readDocument(tenMinutePaths[start + offset], source))
            {
                groupValid = false;
                break;
            }

            if (offset == 0)
            {
                hourlyDocument.set(source);
                hourlyDocument.remove("measurements");
            }

            uint8_t channelMask = 0;
            for (JsonObject measurement : source["measurements"].as<JsonArray>())
            {
                const int channelNumber = measurement["channel"] | 0;
                if (channelNumber < 1 || channelNumber > ADC_CHANNEL_COUNT)
                {
                    groupValid = false;
                    break;
                }

                const uint8_t index = channelNumber - 1;
                const uint8_t channelBit = 1U << index;
                if ((channelMask & channelBit) != 0)
                {
                    groupValid = false;
                    break;
                }
                channelMask |= channelBit;

                Aggregate &aggregate = aggregates[index];
                aggregate.active = true;
                aggregate.deviceId = String(measurement["device_id"] | "");
                aggregate.channel = channelNumber;
                aggregate.sequenceNo = measurement["sequence_no"] | 0;
                aggregate.capturedAt = String(measurement["captured_at"] | "");
                aggregate.adcRaw += measurement["adc_raw"].as<uint32_t>();
                aggregate.adcVoltageMv += measurement["adc_voltage_mv"].as<uint32_t>();
                aggregate.batteryVoltageMv += measurement["battery_voltage_mv"].as<uint32_t>();
                aggregate.batteryPercent += measurement["battery_percent"].as<double>();
                aggregate.wifiRssi += measurement["wifi_rssi"].as<int32_t>();
                aggregate.uptimeSeconds = measurement["uptime_s"] | 0;
                aggregate.count++;
            }

            if (!groupValid)
            {
                break;
            }
            if (offset == 0)
            {
                expectedChannelMask = channelMask;
            }
            else if (channelMask != expectedChannelMask)
            {
                groupValid = false;
                break;
            }
        }

        if (!groupValid || expectedChannelMask == 0)
        {
            break;
        }

        hourlyDocument.remove("aggregation_interval_s");
        hourlyDocument.remove("samples_aggregated");
        hourlyDocument["_aggregation_interval_s"] = 3600;
        hourlyDocument["_samples_aggregated"] = TEN_MINUTE_BATCHES_PER_HOUR;
        JsonArray hourlyMeasurements =
            hourlyDocument["measurements"].to<JsonArray>();

        for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
        {
            const Aggregate &aggregate = aggregates[channel];
            if ((expectedChannelMask & (1U << channel)) == 0)
            {
                continue;
            }
            if (!aggregate.active ||
                aggregate.count != TEN_MINUTE_BATCHES_PER_HOUR)
            {
                groupValid = false;
                break;
            }

            JsonObject measurement = hourlyMeasurements.add<JsonObject>();
            measurement["device_id"] = aggregate.deviceId;
            measurement["channel"] = aggregate.channel;
            measurement["sequence_no"] = aggregate.sequenceNo;
            measurement["captured_at"] = aggregate.capturedAt;
            measurement["adc_raw"] = static_cast<uint32_t>(
                aggregate.adcRaw / aggregate.count);
            measurement["adc_voltage_mv"] = static_cast<uint32_t>(
                aggregate.adcVoltageMv / aggregate.count);
            measurement["battery_voltage_mv"] = static_cast<uint32_t>(
                aggregate.batteryVoltageMv / aggregate.count);
            measurement["battery_percent"] = serialized(
                String(aggregate.batteryPercent / aggregate.count, 1));
            measurement["wifi_rssi"] = static_cast<int32_t>(
                aggregate.wifiRssi / aggregate.count);
            measurement["uptime_s"] = aggregate.uptimeSeconds;
            measurement["is_backfill"] = true;
        }

        if (!groupValid)
        {
            break;
        }

        const String hourlyPath = nextFilePath();
        if (!writeDocument(hourlyPath, hourlyDocument))
        {
            break;
        }

        for (size_t offset = 0; offset < TEN_MINUTE_BATCHES_PER_HOUR; offset++)
        {
            LittleFS.remove(tenMinutePaths[start + offset]);
        }
        compactedHours++;
    }

    if (compactedHours > 0)
    {
        enforceQueueLimit();
    }
    return compactedHours;
}

void telemetryDiscardDevice(const String &deviceId)
{
    if (!storageReady)
    {
        return;
    }

    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        const String path = storagePath(file.name());
        file.close();

        if (isTelemetryFile(path))
        {
            JsonDocument document;
            if (readDocument(path, document))
            {
                JsonArray measurements = document["measurements"].as<JsonArray>();
                for (int index = measurements.size() - 1; index >= 0; index--)
                {
                    if (measurements[index]["device_id"] == deviceId)
                    {
                        measurements.remove(index);
                    }
                }

                if (measurements.size() == 0)
                {
                    LittleFS.remove(path);
                }
                else
                {
                    writeDocument(path, document);
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();
}

uint16_t telemetryQueueCount()
{
    if (!storageReady)
    {
        return 0;
    }

    uint16_t count = 0;
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file)
    {
        if (!file.isDirectory() && isTelemetryFile(file.name()))
        {
            count++;
        }
        file = root.openNextFile();
    }
    root.close();
    return count;
}
