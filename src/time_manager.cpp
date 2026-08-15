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

    // ESP32 SNTP synchronizes automatically when Internet access becomes available.
    Serial.println("[NTP] Background synchronization configured");
}

String getTimestamp()
{
    struct tm timeinfo;

    // Keep offline AP operation responsive; use N/A until background SNTP completes.
    if (!getLocalTime(&timeinfo, 10))
    {
        return "N/A";
    }

    char buffer[25];

    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

    return String(buffer);
}
