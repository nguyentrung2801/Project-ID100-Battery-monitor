#include "record_manager.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

DataRecord records[MAX_RECORDS];
uint8_t recordCount = 0;
unsigned long recordVersion = 0;

static bool recordsDirty = false;
static unsigned long lastRecordChangeMillis = 0;
static const unsigned long RECORD_SAVE_DELAY_MS = 1500;
static const char *RECORD_FILE_PATH = "/records.json";

void addRecord(String timestamp, String rawFrame, String message, String source)
{
    if (recordCount < MAX_RECORDS)
    {
        records[recordCount].timestamp = timestamp;
        records[recordCount].rawFrame = rawFrame;
        records[recordCount].message = message;
        records[recordCount].source = source;

        recordCount++;
    }
    else
    {
        // Keep the newest MAX_RECORDS entries by discarding the oldest one.
        for (int i = 0; i < MAX_RECORDS - 1; i++)
        {
            records[i] = records[i + 1];
        }

        records[MAX_RECORDS - 1].timestamp = timestamp;
        records[MAX_RECORDS - 1].rawFrame = rawFrame;
        records[MAX_RECORDS - 1].message = message;
        records[MAX_RECORDS - 1].source = source;
    }

    // The dashboard uses this version to detect changes without transferring all records.
    recordVersion++;

    recordsDirty = true;
    lastRecordChangeMillis = millis();
}

void processRecordStorage()
{
    if (!recordsDirty)
        return;

    if (millis() - lastRecordChangeMillis < RECORD_SAVE_DELAY_MS)
        return;

    // Debounce flash writes so bursts of UART errors result in a single write.
    saveRecordsToStorage();
    recordsDirty = false;
}

void saveRecordsToStorage()
{
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < recordCount; i++)
    {
        JsonObject obj = arr.add<JsonObject>();

        obj["time"] = records[i].timestamp;
        obj["raw"] = records[i].rawFrame;
        obj["msg"] = records[i].message;
        obj["src"] = records[i].source;
    }

    File file = LittleFS.open(RECORD_FILE_PATH, "w");

    if (!file)
    {
        Serial.println("[FS] Failed to open records.json for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
    recordsDirty = false;

    Serial.println("[FS] Records saved");
}

void loadRecordsFromStorage()
{
    if (!LittleFS.exists(RECORD_FILE_PATH))
    {
        Serial.println("[FS] No saved records");
        return;
    }

    File file = LittleFS.open(RECORD_FILE_PATH, "r");

    if (!file)
    {
        Serial.println("[FS] Failed to open records.json");
        return;
    }

    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.println("[FS] Failed to parse records.json");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();

    recordCount = 0;

    for (JsonObject obj : arr)
    {
        if (recordCount >= MAX_RECORDS)
        {
            break;
        }

        records[recordCount].timestamp = obj["time"].as<String>();
        records[recordCount].rawFrame = obj["raw"].as<String>();
        records[recordCount].message = obj["msg"].as<String>();
        records[recordCount].source = obj["src"].isNull() ? "SMO" : obj["src"].as<String>();

        recordCount++;
    }

    recordVersion++;

    Serial.print("[FS] Records loaded: ");
    Serial.println(recordCount);
}

void clearRecords()
{
    recordCount = 0;
    recordVersion++;
    recordsDirty = false;

    // Write the empty array immediately so cleared data cannot return after a reboot.
    saveRecordsToStorage();

    Serial.println("[FS] Records cleared");
    Serial.println("[WEB] All records cleared");
}

uint8_t getRecordCount()
{
    return recordCount;
}

String getRecordTimestamp(uint8_t index)
{
    if (index >= recordCount)
        return "";

    return records[index].timestamp;
}

String getRecordRawFrame(uint8_t index)
{
    if (index >= recordCount)
        return "";

    return records[index].rawFrame;
}

String getRecordMessage(uint8_t index)
{
    if (index >= recordCount)
        return "";

    return records[index].message;
}

String getRecordSource(uint8_t index)
{
    if (index >= recordCount)
        return "";

    if (records[index].source.length() == 0)
        return "SMO";

    return records[index].source;
}
