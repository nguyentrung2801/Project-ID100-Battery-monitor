#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include <Arduino.h>

void initRelays();
void updateRelays();
void pressRelay(uint8_t relayId);
void loadRelayCounters();
void confirmRelayPressByDoorState(uint8_t doorState);

uint32_t getRelayCount(uint8_t relayId);
uint8_t getRelayCountSize();
void setRelayConfig(uint8_t relayId, uint32_t cycleSec, uint32_t limitCount);
uint32_t getRelayCycle(uint8_t relayId);
uint32_t getRelayLimit(uint8_t relayId);
void resetRelayCount(uint8_t relayId);
uint8_t getRelayPin(uint8_t relayId);
bool isRelayAutoRunning(uint8_t relayId);
bool isAnyRelayAutoRunning();
void manualPressRelay(uint8_t relayId);
bool isRelayFull(uint8_t relayId);
bool isRelayRecentlyPressed(uint8_t relayId);

#endif
