#ifndef CONFIG_H
#define CONFIG_H

#ifndef DEVICE_NAME
#define DEVICE_NAME "JIG-01"
#endif

#ifndef ENABLE_RF_CONTROL
#define ENABLE_RF_CONTROL 1
#endif

#ifndef ENABLE_SMO_SIMULATOR
#define ENABLE_SMO_SIMULATOR 1
#endif

#ifndef ENABLE_SERIAL_LOG
#define ENABLE_SERIAL_LOG 1
#endif

#ifndef BOT_TOKEN
#define BOT_TOKEN "8105256477:AAEwQ4J4aAkNWsjPB0Yzrw8X2xJe5J8vEx0"
#endif

#ifndef CHAT_ID
#define CHAT_ID "-1003309157934"
#endif

#ifndef TELEGRAM_COMMAND_SUFFIX
#define TELEGRAM_COMMAND_SUFFIX "1"
#endif

#ifndef AP_SSID
#define AP_SSID "Auto_Click_JIG01"
#endif

#define AP_PASS "12345678"

#ifndef MDNS_NAME
#define MDNS_NAME "gdo-monitor-01"
#endif

#ifndef PIN_SMO_UART_TX
#define PIN_SMO_UART_TX 5
#endif

#ifndef PIN_SMO_UART_RX
#define PIN_SMO_UART_RX 6
#endif

#ifndef PIN_SB_UART_RX
#define PIN_SB_UART_RX 10
#endif

#ifndef PIN_SB_UART_TX
#define PIN_SB_UART_TX 1
#endif

#define PIN_UART_TX PIN_SMO_UART_TX
#define PIN_UART_RX PIN_SMO_UART_RX

#ifndef NUM_RELAYS
#define NUM_RELAYS 4
#endif

#ifndef RELAY_PULSE_MS
#define RELAY_PULSE_MS 200
#endif

#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 1
#endif

#if RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LEVEL LOW
#define RELAY_INACTIVE_LEVEL HIGH
#else
#define RELAY_ACTIVE_LEVEL HIGH
#define RELAY_INACTIVE_LEVEL LOW
#endif

#define RELAY_CONFIRM_TIMEOUT_MS 15000

#ifndef RELAY_1_PIN
#define RELAY_1_PIN 3
#endif

#ifndef RELAY_2_PIN
#define RELAY_2_PIN 7
#endif

#ifndef RELAY_3_PIN
#define RELAY_3_PIN 8
#endif

#ifndef RELAY_4_PIN
#define RELAY_4_PIN 9
#endif

#define UART_BAUDRATE 115200

#define FRAME_SIZE 11
#define FRAME_QUEUE_SIZE 10
#define MAX_RECORDS 50

#define UART_FRAME_GAP_MS 20

#endif
