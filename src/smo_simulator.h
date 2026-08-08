#ifndef SMO_SIMULATOR_H
#define SMO_SIMULATOR_H

#include <Arduino.h>

struct SbErrorSummary
{
    String event;
    String frame;
    uint32_t count;
};

void setupSmoSimulator(HardwareSerial &serial);
void handleSmoSimulator();
void toggleSmoSimulator();
void startSmoSimulator();
void stopSmoSimulator();
void clearSbLogs();
void clearSmoCycleCount();

bool isSmoSimulatorRunning();
String getSmoSimulatorState();
String getSmoLastCommand();
uint32_t getSmoCycleCount();
uint8_t getSbErrorSummaryCount();
SbErrorSummary getSbErrorSummary(uint8_t index);

#endif
