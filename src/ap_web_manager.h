#ifndef AP_WEB_MANAGER_H
#define AP_WEB_MANAGER_H

#include <Arduino.h>

void setupAPWebServer();

const char *getAPSSID();
const char *getAPPassword();
String getAPURL();

#endif
