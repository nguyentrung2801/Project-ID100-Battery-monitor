#include "identity_manager.h"

#include "config.h"

#include <esp_system.h>

namespace
{
String gatewayId;
String bootId;
String deviceIds[ADC_CHANNEL_COUNT];

String createUuid()
{
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    char value[37];
    snprintf(
        value,
        sizeof(value),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
        bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    return String(value);
}
} // namespace

void identityBegin()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char value[16];
    snprintf(
        value,
        sizeof(value),
        "GW-%02X%02X%02X%02X%02X%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    gatewayId = value;
    bootId = createUuid();
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        deviceIds[channel] = gatewayId + "-CH" + String(channel + 1);
    }
}

const String &getGatewayId()
{
    return gatewayId;
}

const String &getBootId()
{
    return bootId;
}

const String &getDeviceId(uint8_t channel)
{
    return deviceIds[channel < ADC_CHANNEL_COUNT ? channel : 0];
}
