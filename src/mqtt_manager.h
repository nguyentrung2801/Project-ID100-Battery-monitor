#pragma once

#include <Arduino.h>

void mqttBegin();
void mqttLoop();
bool mqttQueueMeasurement(uint8_t channelMask, uint16_t aggregationIntervalSeconds);
bool mqttIsConnected();
String mqttLastStatus();
const String &mqttDesiredTopic();
