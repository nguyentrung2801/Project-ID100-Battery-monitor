#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

void samplingBegin();
void samplingLoop();
void samplingAddStatus(JsonDocument &document);
