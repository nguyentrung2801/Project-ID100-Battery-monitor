#include "settings_manager.h"

#include "config.h"

#include <Preferences.h>
#include <WiFi.h>

namespace
{
constexpr char PREFERENCES_NAMESPACE[] = "id100";
constexpr char KEY_TESTER[] = "tester";
constexpr char KEY_LOCATION[] = "location";
constexpr char KEY_GATEWAY_NAME[] = "gateway_name";
constexpr char KEY_MQTT_HOST[] = "mqtt_host";
constexpr char KEY_MQTT_USERNAME[] = "mqtt_user";
constexpr char KEY_MQTT_PASSWORD[] = "mqtt_pass";
constexpr char KEY_MQTT_REVISION[] = "mqtt_rev";
constexpr char KEY_WIFI_SSID[] = "wifi_ssid";
constexpr char KEY_WIFI_PASSWORD[] = "wifi_pass";

Preferences preferences;
AppSettings currentSettings;
} // namespace

void settingsBegin()
{
    preferences.begin(PREFERENCES_NAMESPACE, false);

    currentSettings.gatewayName = preferences.getString(KEY_GATEWAY_NAME, "");
    currentSettings.testerName = preferences.getString(KEY_TESTER, "");
    currentSettings.testLocation = preferences.getString(KEY_LOCATION, "");
    // Remove the legacy optional label setting. Gateway name is now the only
    // user-facing identifier configured before a test.
    if (preferences.isKey("label"))
    {
        preferences.remove("label");
    }
    currentSettings.mqttHost = preferences.getString(KEY_MQTT_HOST, MQTT_HOST);
    currentSettings.mqttUsername = preferences.getString(KEY_MQTT_USERNAME, MQTT_USERNAME);
    currentSettings.mqttPassword = preferences.getString(KEY_MQTT_PASSWORD, MQTT_PASSWORD);

    const bool mqttConfigMissing = !preferences.isKey(KEY_MQTT_HOST);
    const bool mqttConfigIsOldPlaceholder = currentSettings.mqttHost == "mqtt.example.com";
    const uint32_t savedMqttRevision = preferences.getUInt(KEY_MQTT_REVISION, 0);
    const bool mqttConfigNeedsUpgrade = savedMqttRevision != MQTT_CONFIG_REVISION;
    if (mqttConfigMissing || mqttConfigIsOldPlaceholder || mqttConfigNeedsUpgrade)
    {
        currentSettings.mqttHost = MQTT_HOST;
        currentSettings.mqttUsername = MQTT_USERNAME;
        currentSettings.mqttPassword = MQTT_PASSWORD;
        preferences.putString(KEY_MQTT_HOST, currentSettings.mqttHost);
        preferences.putString(KEY_MQTT_USERNAME, currentSettings.mqttUsername);
        preferences.putString(KEY_MQTT_PASSWORD, currentSettings.mqttPassword);
        preferences.putUInt(KEY_MQTT_REVISION, MQTT_CONFIG_REVISION);
    }
}

const AppSettings &settingsGet()
{
    return currentSettings;
}

bool settingsTestInformationComplete()
{
    return !currentSettings.gatewayName.isEmpty() &&
           !currentSettings.testerName.isEmpty() &&
           !currentSettings.testLocation.isEmpty();
}

void settingsSave(const AppSettings &settings)
{
    currentSettings = settings;
    currentSettings.gatewayName.trim();
    currentSettings.testerName.trim();
    currentSettings.testLocation.trim();

    preferences.putString(KEY_GATEWAY_NAME, currentSettings.gatewayName);
    preferences.putString(KEY_TESTER, currentSettings.testerName);
    preferences.putString(KEY_LOCATION, currentSettings.testLocation);
    preferences.putString(KEY_MQTT_HOST, settings.mqttHost);
    preferences.putString(KEY_MQTT_USERNAME, settings.mqttUsername);
    preferences.putString(KEY_MQTT_PASSWORD, settings.mqttPassword);
}

void connectSavedWifi()
{
    const String ssid = preferences.getString(KEY_WIFI_SSID, "");
    if (ssid.isEmpty())
    {
        return;
    }

    const String password = preferences.getString(KEY_WIFI_PASSWORD, "");
    WiFi.begin(ssid.c_str(), password.c_str());
}

void saveWifi(const String &ssid, const String &password)
{
    preferences.putString(KEY_WIFI_SSID, ssid);
    preferences.putString(KEY_WIFI_PASSWORD, password);
    WiFi.begin(ssid.c_str(), password.c_str());
}

void clearWifi()
{
    preferences.remove(KEY_WIFI_SSID);
    preferences.remove(KEY_WIFI_PASSWORD);
    WiFi.disconnect(false, true);
}
