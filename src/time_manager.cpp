#include "time_manager.h"

#include <time.h>

void setupTime()
{
    configTime(
        7 * 3600, // GMT+7 (Vietnam time)
        0,        // No daylight saving time
        "pool.ntp.org",
        "time.google.com"
    );

    struct tm timeinfo;

    // Timestamps are used in persisted logs, so setup waits for a valid clock.
    while (!getLocalTime(&timeinfo))
    {
        Serial.println("[NTP] Waiting for time sync...");
        delay(500);
    }

    Serial.println("[NTP] Time synchronized");
}

String getTimestamp()
{
    struct tm timeinfo;

    if (!getLocalTime(&timeinfo))
    {
        return "N/A";
    }

    char buffer[25];

    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

    return String(buffer);
}
