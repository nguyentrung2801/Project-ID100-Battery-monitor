#ifndef TELEGRAM_MANAGER_H
#define TELEGRAM_MANAGER_H

#include <Arduino.h>

void sendTelegram(String msg);
void sendTelegramHTML(String msg);
void sendTelegramPriority(String msg);
void sendTelegramHTMLPriority(String msg);
bool sendTelegramHTMLNow(String msg);
String getLastTelegramSendStatus();
void beginTelegramTask();
void processTelegramQueue();
String urlEncode(String str);

#endif
