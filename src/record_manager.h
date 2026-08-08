#ifndef RECORD_MANAGER_H
#define RECORD_MANAGER_H

#include <Arduino.h>
#include <config.h>

struct DataRecord
{
    String timestamp;
    String rawFrame;
    String message;
    String source;
};

uint8_t getRecordCount();
String getRecordTimestamp(uint8_t index);
String getRecordRawFrame(uint8_t index);
String getRecordMessage(uint8_t index);
String getRecordSource(uint8_t index);

extern unsigned long recordVersion;
extern DataRecord records[MAX_RECORDS];
extern uint8_t recordCount;

void addRecord(String timestamp, String rawFrame, String message, String source = "SMO");
void processRecordStorage();
void saveRecordsToStorage();
void loadRecordsFromStorage();
void clearRecords();

#endif
