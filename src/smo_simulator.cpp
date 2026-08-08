#include "smo_simulator.h"
#include "config.h"
#include "record_manager.h"
#include "telegram_manager.h"
#include "time_manager.h"

#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

static const uint8_t SB_FRAME_MAX_SIZE = 16;
static const uint8_t SB_ERROR_SUMMARY_SIZE = 12;
static const char *SB_ERROR_FILE_PATH = "/sb_errors.json";
static const char *SMO_STATE_FILE_PATH = "/smo_state.json";
static const unsigned long SB_ERROR_SAVE_DELAY_MS = 1500;

static const unsigned long FIRST_OK_TIMEOUT_MS = 6500;
static const unsigned long CLOSE_MONITOR_MS = 10000;
static const unsigned long IR_OK_GAP_TIMEOUT_MS = 2000;
static const unsigned long SLEEP_TO_OPEN_DELAY_MS = 2000;
static const unsigned long OPEN_TO_WAKE_DELAY_MS = 6000;

enum SmoState
{
    SMO_STOPPED,
    SMO_CLOSING_MONITOR,
    SMO_WAIT_OPEN_COMMAND,
    SMO_WAIT_WAKE_AFTER_OPEN
};

static HardwareSerial *smoSerial = nullptr;
static SmoState smoState = SMO_STOPPED;
static bool simulatorRunning = false;
static bool sawIrOk = false;
static bool pendingOpenAfterSleep = false;
static unsigned long stateStartMillis = 0;
static unsigned long lastIrOkMillis = 0;
static uint32_t smoCycleCount = 0;
static String lastCommand = "None";

static SbErrorSummary sbErrors[SB_ERROR_SUMMARY_SIZE];
static uint8_t sbErrorCount = 0;

static uint8_t rxBuf[SB_FRAME_MAX_SIZE];
static uint8_t rxIndex = 0;
static int expectedLength = -1;
static unsigned long lastByteMillis = 0;
static bool sbErrorsDirty = false;
static unsigned long lastSbErrorChangeMillis = 0;

static void sendDoorOpening();
static void startClosingCycle();
static void handleSbFrame(uint8_t *frame, uint8_t length);
static void saveSbErrorsToStorage();
static void loadSbErrorsFromStorage();
static void markSbErrorsDirty();
static void processSbErrorStorage();
static void saveSmoStateToStorage();
static bool loadSmoStateFromStorage();
static void resumeSmoSimulatorAfterBoot();

static void recordSbError(const String &event, const String &frame)
{
    for (uint8_t i = 0; i < sbErrorCount; i++)
    {
        if (sbErrors[i].event == event)
        {
            sbErrors[i].frame = frame;
            sbErrors[i].count++;
            String timestamp = getTimestamp();
            addRecord(timestamp, frame, event, "SB");
            markSbErrorsDirty();
            sendTelegramHTML("<b>[" + timestamp + "]</b>\n" + event + "\n<code>" + frame + "</code>\nSource: SB");
            return;
        }
    }

    if (sbErrorCount < SB_ERROR_SUMMARY_SIZE)
    {
        sbErrors[sbErrorCount].event = event;
        sbErrors[sbErrorCount].frame = frame;
        sbErrors[sbErrorCount].count = 1;
        sbErrorCount++;
    }

    String timestamp = getTimestamp();
    addRecord(timestamp, frame, event, "SB");
    markSbErrorsDirty();
    sendTelegramHTML("<b>[" + timestamp + "]</b>\n" + event + "\n<code>" + frame + "</code>\nSource: SB");
}

static void markSbErrorsDirty()
{
    sbErrorsDirty = true;
    lastSbErrorChangeMillis = millis();
}

static void processSbErrorStorage()
{
    if (!sbErrorsDirty)
        return;

    if (millis() - lastSbErrorChangeMillis < SB_ERROR_SAVE_DELAY_MS)
        return;

    // Debounce LittleFS writes because a fault can produce several UART frames.
    saveSbErrorsToStorage();
}

static void saveSbErrorsToStorage()
{
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < sbErrorCount; i++)
    {
        JsonObject obj = arr.add<JsonObject>();
        obj["event"] = sbErrors[i].event;
        obj["frame"] = sbErrors[i].frame;
        obj["count"] = sbErrors[i].count;
    }

    File file = LittleFS.open(SB_ERROR_FILE_PATH, "w");
    if (!file)
    {
        Serial.println("[FS] Failed to open sb_errors.json for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
    sbErrorsDirty = false;
}

static void loadSbErrorsFromStorage()
{
    if (!LittleFS.exists(SB_ERROR_FILE_PATH))
    {
        Serial.println("[FS] No saved SB error summary");
        return;
    }

    File file = LittleFS.open(SB_ERROR_FILE_PATH, "r");
    if (!file)
    {
        Serial.println("[FS] Failed to open sb_errors.json");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.println("[FS] Failed to parse sb_errors.json");
        return;
    }

    sbErrorCount = 0;
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr)
    {
        if (sbErrorCount >= SB_ERROR_SUMMARY_SIZE)
            break;

        sbErrors[sbErrorCount].event = obj["event"].as<String>();
        sbErrors[sbErrorCount].frame = obj["frame"].as<String>();
        sbErrors[sbErrorCount].count = obj["count"] | 0;
        sbErrorCount++;
    }

    Serial.print("[FS] SB error summary loaded: ");
    Serial.println(sbErrorCount);
}

static void saveSmoStateToStorage()
{
    JsonDocument doc;
    doc["running"] = simulatorRunning;
    doc["cycles"] = smoCycleCount;

    File file = LittleFS.open(SMO_STATE_FILE_PATH, "w");
    if (!file)
    {
        Serial.println("[FS] Failed to open smo_state.json for writing");
        return;
    }

    serializeJson(doc, file);
    file.close();
}

static bool loadSmoStateFromStorage()
{
    if (!LittleFS.exists(SMO_STATE_FILE_PATH))
    {
        Serial.println("[FS] No saved SMO simulator state");
        return false;
    }

    File file = LittleFS.open(SMO_STATE_FILE_PATH, "r");
    if (!file)
    {
        Serial.println("[FS] Failed to open smo_state.json");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.println("[FS] Failed to parse smo_state.json");
        return false;
    }

    smoCycleCount = doc["cycles"] | 0;
    bool wasRunning = doc["running"] | false;

    Serial.print("[FS] SMO simulator cycles loaded: ");
    Serial.println(smoCycleCount);
    Serial.print("[FS] SMO simulator was running: ");
    Serial.println(wasRunning ? "yes" : "no");

    return wasRunning;
}

static void resumeSmoSimulatorAfterBoot()
{
    // Resume from the safe open/wake phase instead of restoring a partially elapsed timer.
    simulatorRunning = true;
    sawIrOk = false;
    lastIrOkMillis = 0;
    pendingOpenAfterSleep = false;
    stateStartMillis = millis();
    smoState = SMO_WAIT_WAKE_AFTER_OPEN;

    Serial.println("[SMO] Simulator resumed after boot");
    sendDoorOpening();
}

static String bytesToHex(const uint8_t *data, uint8_t length)
{
    String result = "";

    for (uint8_t i = 0; i < length; i++)
    {
        char hex[4];
        sprintf(hex, "%02X ", data[i]);
        result += hex;
    }

    return result;
}

static bool validateFrame(const uint8_t *frame, uint8_t length)
{
    if (length < 9)
        return false;

    if (frame[0] != 0xAA || frame[1] != 0xAA)
        return false;

    const uint8_t dataLength = frame[5];
    const uint8_t checksumIndex = 6 + dataLength;

    if (length != dataLength + 9)
        return false;

    if (frame[checksumIndex + 1] != 0x55 || frame[checksumIndex + 2] != 0x55)
        return false;

    uint8_t checksum = 0;
    for (uint8_t i = 2; i <= 5 + dataLength; i++)
    {
        checksum ^= frame[i];
    }

    return checksum == frame[checksumIndex];
}

static String decodeSbEvent(const uint8_t *frame, uint8_t length, bool valid)
{
    if (!valid)
        return "Invalid SB frame";

    uint8_t comm = frame[4];
    uint8_t dataLength = frame[5];
    uint8_t data = dataLength > 0 ? frame[6] : 0xFF;

    if (comm == 0x00 && dataLength == 0)
        return "SB ACK";

    if (comm == 0x01 && dataLength == 1)
    {
        if (data == 0x03)
            return "Pair with SMO successful";
        if (data >= 0x04 && data <= 0x08)
            return "RF channel status";
        return "SB status update";
    }

    if (comm == 0x02 && dataLength == 1)
    {
        switch (data)
        {
        case 0x00:
            return "Wireless IR OK";
        case 0x01:
            return "Wireless IR Block";
        case 0x02:
            return "Wireless IR Slave Battery Low";
        case 0x03:
            return "Wireless IR Master Battery Low";
        case 0x04:
            return "Wireless IR Master Lost";
        case 0x05:
            return "Wireless IR Slave Lost";
        case 0x06:
            return "Wireless IR Master Recover";
        case 0x07:
            return "Wireless IR Slave Recover";
        case 0x08:
            return "Wireless IR Slave-Master Lost";
        default:
            return "Wireless IR other status";
        }
    }

    return "SB frame";
}

static void sendFrame(uint8_t data, const char *name)
{
    if (smoSerial == nullptr)
        return;

    uint8_t frame[10] = {
        0xAA, 0xAA,
        0x01, 0x01, 0x00, 0x01,
        data, 0x00,
        0x55, 0x55};

    frame[7] = frame[2] ^ frame[3] ^ frame[4] ^ frame[5] ^ frame[6];
    smoSerial->write(frame, sizeof(frame));
    smoSerial->flush();

    lastCommand = name;

    Serial.print("[SMO TX] ");
    Serial.print(name);
    Serial.print(" ");
    Serial.println(bytesToHex(frame, sizeof(frame)));
}

static void sendWake()
{
    sendFrame(0x01, "Wake-up");
}

static void sendSleep()
{
    sendFrame(0x00, "Sleep");
}

static void sendDoorOpening()
{
    sendFrame(0x04, "Door Opening");
}

static void sendFullyClose()
{
    sendFrame(0x05, "Fully Close");
}

static void startClosingCycle()
{
    if (!simulatorRunning)
        return;

    sawIrOk = false;
    lastIrOkMillis = 0;
    stateStartMillis = millis();
    smoState = SMO_CLOSING_MONITOR;
    sendWake();
}

static void scheduleOpenAfterSleep()
{
    sendSleep();
    pendingOpenAfterSleep = true;
    stateStartMillis = millis();
    smoState = SMO_WAIT_OPEN_COMMAND;
}

static void finishCloseCycle()
{
    sendSleep();
    sendFullyClose();
    smoCycleCount++;
    saveSmoStateToStorage();
    pendingOpenAfterSleep = false;
    stateStartMillis = millis();
    smoState = SMO_WAIT_OPEN_COMMAND;
}

static void readSbUart()
{
    if (smoSerial == nullptr)
        return;

    while (smoSerial->available())
    {
        uint8_t value = smoSerial->read();
        unsigned long now = millis();

        if (now - lastByteMillis > UART_FRAME_GAP_MS)
        {
            rxIndex = 0;
            expectedLength = -1;
        }

        lastByteMillis = now;

        if (rxIndex == 0 && value != 0xAA)
            continue;

        if (rxIndex == 1 && value != 0xAA)
        {
            rxIndex = 0;
            expectedLength = -1;
            continue;
        }

        if (rxIndex >= SB_FRAME_MAX_SIZE)
        {
            rxIndex = 0;
            expectedLength = -1;
            continue;
        }

        rxBuf[rxIndex++] = value;

        if (rxIndex == 6)
        {
            expectedLength = rxBuf[5] + 9;
            if (expectedLength > SB_FRAME_MAX_SIZE)
            {
                recordSbError("SB frame too long", bytesToHex(rxBuf, rxIndex));
                rxIndex = 0;
                expectedLength = -1;
            }
        }

        if (expectedLength > 0 && rxIndex >= expectedLength)
        {
            handleSbFrame(rxBuf, rxIndex);
            rxIndex = 0;
            expectedLength = -1;
        }
    }
}

static void drainSbUart()
{
    if (smoSerial == nullptr)
        return;

    while (smoSerial->available())
    {
        smoSerial->read();
    }

    rxIndex = 0;
    expectedLength = -1;
}

static void handleSbFrame(uint8_t *frame, uint8_t length)
{
    bool valid = validateFrame(frame, length);
    String event = decodeSbEvent(frame, length, valid);

    Serial.print("[SB RX] ");
    Serial.print(event);
    Serial.print(" ");
    Serial.println(bytesToHex(frame, length));

    if (!valid)
    {
        recordSbError("Invalid SB frame", bytesToHex(frame, length));
        return;
    }

    // Frames are still decoded and logged outside this state, but only IR status
    // frames received during close monitoring are allowed to drive the state machine.
    if (smoState != SMO_CLOSING_MONITOR)
        return;

    bool isIrStatus = frame[4] == 0x02 && frame[5] == 0x01;
    if (!isIrStatus)
        return;

    if (frame[6] == 0x00)
    {
        unsigned long now = millis();
        if (!sawIrOk)
        {
            // The 10-second close window begins with the first valid IR OK.
            stateStartMillis = now;
        }

        sawIrOk = true;
        lastIrOkMillis = now;
        return;
    }

    if (frame[6] == 0x01)
    {
        Serial.println("[SMO] IR Block received. Restarting cycle.");
        String rawFrame = bytesToHex(frame, length);
        recordSbError("Wireless IR Block", rawFrame);

        scheduleOpenAfterSleep();
        return;
    }

    recordSbError(event, bytesToHex(frame, length));
}

void setupSmoSimulator(HardwareSerial &serial)
{
    smoSerial = &serial;
    loadSbErrorsFromStorage();

    if (loadSmoStateFromStorage())
    {
        resumeSmoSimulatorAfterBoot();
    }
}

void handleSmoSimulator()
{
    processSbErrorStorage();

    if (!simulatorRunning)
    {
        drainSbUart();
        return;
    }

    readSbUart();

    unsigned long now = millis();

    switch (smoState)
    {
    case SMO_CLOSING_MONITOR:
        if (!sawIrOk && now - stateStartMillis >= FIRST_OK_TIMEOUT_MS)
        {
            Serial.println("[SMO] No IR OK within 6.5s. Restarting cycle.");
            recordSbError("Wireless IR Timeout", "No IR OK within 6.5s after Wake-up");

            scheduleOpenAfterSleep();
        }
        else if (sawIrOk && now - lastIrOkMillis >= IR_OK_GAP_TIMEOUT_MS)
        {
            Serial.println("[SMO] No follow-up IR OK within 2s. Restarting cycle.");
            recordSbError("Wireless IR Timeout", "No follow-up IR OK within 2s during close monitor");

            scheduleOpenAfterSleep();
        }
        else if (now - stateStartMillis >= CLOSE_MONITOR_MS)
        {
            finishCloseCycle();
        }
        break;

    case SMO_WAIT_OPEN_COMMAND:
        if (now - stateStartMillis >= SLEEP_TO_OPEN_DELAY_MS)
        {
            sendDoorOpening();
            pendingOpenAfterSleep = false;
            stateStartMillis = millis();
            smoState = SMO_WAIT_WAKE_AFTER_OPEN;
        }
        break;

    case SMO_WAIT_WAKE_AFTER_OPEN:
        if (now - stateStartMillis >= OPEN_TO_WAKE_DELAY_MS)
        {
            startClosingCycle();
        }
        break;

    case SMO_STOPPED:
    default:
        break;
    }
}

void toggleSmoSimulator()
{
    if (simulatorRunning)
        stopSmoSimulator();
    else
        startSmoSimulator();
}

void startSmoSimulator()
{
    if (simulatorRunning)
        return;

    simulatorRunning = true;
    Serial.println("[SMO] Simulator started");
    sendDoorOpening();
    pendingOpenAfterSleep = false;
    stateStartMillis = millis();
    smoState = SMO_WAIT_WAKE_AFTER_OPEN;
    saveSmoStateToStorage();
}

void stopSmoSimulator()
{
    simulatorRunning = false;
    smoState = SMO_STOPPED;
    sawIrOk = false;
    lastIrOkMillis = 0;
    pendingOpenAfterSleep = false;
    Serial.println("[SMO] Simulator stopped");
    saveSmoStateToStorage();
}

void clearSbLogs()
{
    sbErrorCount = 0;
    sbErrorsDirty = false;
    saveSbErrorsToStorage();
}

void clearSmoCycleCount()
{
    smoCycleCount = 0;
    saveSmoStateToStorage();
}

bool isSmoSimulatorRunning()
{
    return simulatorRunning;
}

String getSmoSimulatorState()
{
    switch (smoState)
    {
    case SMO_CLOSING_MONITOR:
        return "Closing monitor";
    case SMO_WAIT_OPEN_COMMAND:
        return pendingOpenAfterSleep ? "Sleep wait open" : "Fully close wait open";
    case SMO_WAIT_WAKE_AFTER_OPEN:
        return "Open wait wake-up";
    case SMO_STOPPED:
    default:
        return "Stopped";
    }
}

String getSmoLastCommand()
{
    return lastCommand;
}

uint32_t getSmoCycleCount()
{
    return smoCycleCount;
}

uint8_t getSbErrorSummaryCount()
{
    return sbErrorCount;
}

SbErrorSummary getSbErrorSummary(uint8_t index)
{
    if (index >= sbErrorCount)
    {
        return {"", "", 0};
    }

    return sbErrors[index];
}
