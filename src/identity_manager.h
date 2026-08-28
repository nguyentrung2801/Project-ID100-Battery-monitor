#pragma once

#include <Arduino.h>

void identityBegin();
const String &getGatewayId();
const String &getBootId();
const String &getDeviceId(uint8_t channel);
