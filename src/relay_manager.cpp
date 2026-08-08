#include "relay_manager.h"
#include "config.h"

#include <Preferences.h>

static const uint8_t relayPins[NUM_RELAYS] =
{
    RELAY_1_PIN,
#if NUM_RELAYS > 1
    RELAY_2_PIN,
#endif
#if NUM_RELAYS > 2
    RELAY_3_PIN,
#endif
#if NUM_RELAYS > 3
    RELAY_4_PIN,
#endif
};

static uint32_t relayCounter[NUM_RELAYS] = {0};
static uint32_t relayPressCounter[NUM_RELAYS] = {0};
static bool relayActive[NUM_RELAYS] = {false};
static unsigned long relayStartMillis[NUM_RELAYS] = {0};
static uint32_t relayCycleSec[NUM_RELAYS] = {0};
static uint32_t relayLimit[NUM_RELAYS] = {0};
static unsigned long relayLastAutoMillis[NUM_RELAYS] = {0};
static unsigned long relayLastPressMillis[NUM_RELAYS] = {0};
static bool relayPendingConfirm[NUM_RELAYS] = {false};
static unsigned long relayPendingMillis[NUM_RELAYS] = {0};

extern Preferences preferences;

static bool isAnyRelayBusy()
{
    for (uint8_t i = 0; i < NUM_RELAYS; i++)
    {
        if (relayActive[i] || relayPendingConfirm[i])
            return true;
    }

    return false;
}

void initRelays()
{
    for (uint8_t i = 0; i < NUM_RELAYS; i++)
    {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], RELAY_INACTIVE_LEVEL);
    }
}

void loadRelayCounters()
{
    for (uint8_t i = 0; i < NUM_RELAYS; i++)
    {
        relayCounter[i] = preferences.getUInt(("relayCnt" + String(i)).c_str(), 0);
        relayPressCounter[i] = preferences.getUInt(("relayPress" + String(i)).c_str(), relayCounter[i] * 2);
        relayCycleSec[i] = preferences.getUInt(("relayCyc" + String(i)).c_str(), 0);
        relayLimit[i] = preferences.getUInt(("relayLim" + String(i)).c_str(), 0);
    }
}

static void saveRelayCounter(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return;

    String key = "relayCnt" + String(relayId);
    preferences.putUInt(key.c_str(), relayCounter[relayId]);
}

static void saveRelayPressCounter(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return;

    String key = "relayPress" + String(relayId);
    preferences.putUInt(key.c_str(), relayPressCounter[relayId]);
}

static void countRelayDirect(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return;

    relayCounter[relayId]++;
    relayPressCounter[relayId]++;
    saveRelayCounter(relayId);
    saveRelayPressCounter(relayId);

    Serial.print("[RELAY] Relay ");
    Serial.print(relayId + 1);
    Serial.print(" counted by press. Count = ");
    Serial.println(relayCounter[relayId]);
}
static void countRelayPressMode(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return;

    relayPressCounter[relayId]++;
    saveRelayPressCounter(relayId);

    // One door cycle uses two button presses; odd presses represent close commands.
    if ((relayPressCounter[relayId] % 2) == 1)
    {
        relayCounter[relayId]++;
        saveRelayCounter(relayId);

        Serial.print("[RELAY] Relay ");
        Serial.print(relayId + 1);
        Serial.print(" counted by close press. Count = ");
        Serial.println(relayCounter[relayId]);
    }
}

void pressRelay(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return;

    if (relayLimit[relayId] > 1 && relayCounter[relayId] >= relayLimit[relayId])
    {
        Serial.println("[RELAY] Limit reached");
        return;
    }
    digitalWrite(relayPins[relayId], RELAY_ACTIVE_LEVEL);
    relayActive[relayId] = true;
    relayStartMillis[relayId] = millis();

#if defined(RELAY_COUNT_ON_PRESS) && RELAY_COUNT_ON_PRESS
    countRelayDirect(relayId);
#else
    countRelayPressMode(relayId);
#endif

    relayLastPressMillis[relayId] = millis();

    Serial.print("[RELAY] Relay ");
    Serial.print(relayId + 1);
#if defined(RELAY_COUNT_ON_PRESS) && RELAY_COUNT_ON_PRESS
    Serial.println(" pressed. Count increased by press");
#else
    Serial.println(" pressed. Count uses odd close presses");
#endif
}

void updateRelays()
{
    for (uint8_t i = 0; i < NUM_RELAYS; i++)
    {
        if (relayActive[i] && millis() - relayStartMillis[i] >= RELAY_PULSE_MS)
        {
            digitalWrite(relayPins[i], RELAY_INACTIVE_LEVEL);
            relayActive[i] = false;
            Serial.print("[RELAY] Relay ");
            Serial.print(i + 1);
            Serial.println(" released");
        }

        if (relayPendingConfirm[i] && millis() - relayPendingMillis[i] >= RELAY_CONFIRM_TIMEOUT_MS)
        {
            relayPendingConfirm[i] = false;
            Serial.print("[RELAY] Relay ");
            Serial.print(i + 1);
            Serial.println(" confirmation timeout. Count not increased");
        }
    }

    // Serialize automatic presses so relay pulses and confirmations cannot overlap.
    if (isAnyRelayBusy())
        return;

    for (uint8_t i = 0; i < NUM_RELAYS; i++)
    {
        if (relayCycleSec[i] == 0)
            continue;

        if (relayLimit[i] == 1)
            continue;

        if (relayLimit[i] >= 2 && relayCounter[i] >= relayLimit[i])
            continue;

        if (millis() - relayLastAutoMillis[i] >= relayCycleSec[i] * 1000UL)
        {
            relayLastAutoMillis[i] = millis();
            pressRelay(i);
        }
    }
}

void confirmRelayPressByDoorState(uint8_t doorState)
{
    if (doorState != 3)
        return;

    // If multiple confirmations are pending, attribute the close event to the oldest.
    int selected = -1;
    unsigned long oldestElapsed = 0;
    unsigned long now = millis();

    for (uint8_t i = 0; i < NUM_RELAYS; i++)
    {
        if (!relayPendingConfirm[i])
            continue;

        unsigned long elapsed = now - relayPendingMillis[i];
        if (elapsed >= RELAY_CONFIRM_TIMEOUT_MS)
            continue;

        if (selected < 0 || elapsed > oldestElapsed)
        {
            selected = i;
            oldestElapsed = elapsed;
        }
    }

    if (selected < 0)
        return;

    relayPendingConfirm[selected] = false;
    relayCounter[selected]++;
    saveRelayCounter(selected);

    Serial.print("[RELAY] Relay ");
    Serial.print(selected + 1);
    Serial.print(" confirmed by SMO door closing. Count = ");
    Serial.println(relayCounter[selected]);
}

uint32_t getRelayCount(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return 0;

    return relayCounter[relayId];
}

uint8_t getRelayCountSize()
{
    return NUM_RELAYS;
}

void setRelayConfig(uint8_t relayId, uint32_t cycleSec, uint32_t limitCount)
{
    if (relayId >= NUM_RELAYS)
        return;

    relayCycleSec[relayId] = cycleSec;
    relayLimit[relayId] = limitCount;

    preferences.putUInt(("relayCyc" + String(relayId)).c_str(), cycleSec);
    preferences.putUInt(("relayLim" + String(relayId)).c_str(), limitCount);

    relayLastAutoMillis[relayId] = millis();
}

uint32_t getRelayCycle(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return 0;

    return relayCycleSec[relayId];
}

uint32_t getRelayLimit(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return 0;

    return relayLimit[relayId];
}

void resetRelayCount(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return;

    relayCounter[relayId] = 0;
    relayPressCounter[relayId] = 0;
    saveRelayCounter(relayId);
    saveRelayPressCounter(relayId);
}

uint8_t getRelayPin(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return 0;

    return relayPins[relayId];
}

bool isRelayAutoRunning(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return false;

    if (relayCycleSec[relayId] == 0)
        return false;

    if (relayLimit[relayId] == 1)
        return false;

    if (relayLimit[relayId] >= 2 && relayCounter[relayId] >= relayLimit[relayId])
        return false;

    return true;
}

bool isAnyRelayAutoRunning()
{
    for (uint8_t i = 0; i < NUM_RELAYS; i++)
    {
        if (isRelayAutoRunning(i))
            return true;
    }

    return false;
}

void manualPressRelay(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return;

    pressRelay(relayId);
    relayLastAutoMillis[relayId] = millis();
}

bool isRelayFull(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return false;

    if (relayLimit[relayId] == 1)
        return true;

    return relayLimit[relayId] >= 2 && relayCounter[relayId] >= relayLimit[relayId];
}

bool isRelayRecentlyPressed(uint8_t relayId)
{
    if (relayId >= NUM_RELAYS)
        return false;

    return (millis() - relayLastPressMillis[relayId] <= 800);
}
