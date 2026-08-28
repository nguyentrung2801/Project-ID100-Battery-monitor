#pragma once

#include <Arduino.h>

struct AppSettings
{
    String gatewayName;
    String testerName;
    String testLocation;
    String mqttHost;
    String mqttUsername;
    String mqttPassword;
};

void settingsBegin();
const AppSettings &settingsGet();
bool settingsTestInformationComplete();
void settingsSave(const AppSettings &settings);

void connectSavedWifi();
void saveWifi(const String &ssid, const String &password);
void clearWifi();
