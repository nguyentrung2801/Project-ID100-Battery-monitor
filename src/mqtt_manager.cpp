#include "mqtt_manager.h"

#include "command_manager.h"
#include "battery_manager.h"
#include "config.h"
#include "identity_manager.h"
#include "settings_manager.h"
#include "telemetry_manager.h"
#include "time_manager.h"

#include <WiFi.h>
#include <ArduinoJson.h>
#include <mqtt_client.h>

namespace
{
esp_mqtt_client_handle_t client = nullptr;
bool clientStarted = false;
volatile bool connected = false;
volatile bool subscribed = false;
volatile int pendingTelemetryMessageId = -1;
volatile int pendingBootstrapMessageId = -1;
volatile bool markBackfillRequested = false;
volatile bool bootstrapRequested = false;
bool bootstrapPublished = false;
bool hasBeenMqttOnline = false;
bool offlineBuffering = false;
unsigned long offlineStartedMillis = 0;
unsigned long lastCompactionCheckMillis = 0;

unsigned long nextReconnectMillis = 0;
unsigned long reconnectDelayMillis = MQTT_RECONNECT_MIN_MS;
String telemetryTopic;
String desiredTopic;
String statusTopic;
String brokerUri;
String lastWillPayload;
String inFlightFile;
String incomingTopic;
String incomingPayload;
String lastStatus = "NOT_STARTED";

bool anyChannelRunning()
{
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        if (commandChannelState(channel) == ChannelState::Running)
        {
            return true;
        }
    }
    return false;
}

String gatewayStatusPayload(const char *status)
{
    JsonDocument document;
    document["schema_version"] = SCHEMA_VERSION;
    document["type"] = "gateway_status";
    document["gateway_id"] = getGatewayId();
    document["status"] = status;

    if (strcmp(status, "offline") == 0)
    {
        document["reason"] = "connection_lost";
    }
    else
    {
        document["gateway_name"] = settingsGet().gatewayName;
        document["firmware_version"] = FIRMWARE_VERSION;
        document["boot_id"] = getBootId();
    }

    String payload;
    serializeJson(document, payload);
    return payload;
}

void scheduleReconnect()
{
    nextReconnectMillis = millis() + reconnectDelayMillis;
    reconnectDelayMillis = min(
        reconnectDelayMillis * 2,
        static_cast<unsigned long>(MQTT_RECONNECT_MAX_MS));
}

void handleDesiredMessage(const String &topic, const String &payload)
{
    Serial.printf("[MQTT] RX topic: %s\n", topic.c_str());
    Serial.printf("[MQTT] RX payload: %s\n", payload.c_str());
    const CommandResponse response = commandHandle(topic, payload);
    if (!response.valid || !connected)
    {
        Serial.println("[MQTT] Command ignored");
        return;
    }

    esp_mqtt_client_enqueue(
        client,
        response.topic.c_str(),
        response.payload.c_str(),
        response.payload.length(),
        1,
        0,
        true);
}

void collectIncomingData(esp_mqtt_event_handle_t event)
{
    if (event->current_data_offset == 0)
    {
        incomingTopic = String(event->topic, event->topic_len);
        incomingPayload = "";
        incomingPayload.reserve(event->total_data_len);
    }

    incomingPayload.concat(event->data, event->data_len);
    const bool complete = event->current_data_offset + event->data_len >=
                          event->total_data_len;
    if (complete)
    {
        handleDesiredMessage(incomingTopic, incomingPayload);
    }
}

esp_err_t mqttEventHandler(esp_mqtt_event_handle_t event)
{
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        connected = true;
        subscribed = false;
        reconnectDelayMillis = MQTT_RECONNECT_MIN_MS;
        lastStatus = "CONNECTED";
        Serial.printf("[MQTT] Connected as %s\n", getGatewayId().c_str());
        Serial.printf("[MQTT] Subscribe: %s\n", desiredTopic.c_str());
        esp_mqtt_client_subscribe(client, desiredTopic.c_str(), 1);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        subscribed = true;
        lastStatus = "ONLINE";
        Serial.printf("[MQTT] Subscribe ACK, msg_id=%d\n", event->msg_id);
        {
            const String onlinePayload = gatewayStatusPayload("online");
            esp_mqtt_client_enqueue(
                client,
                statusTopic.c_str(),
                onlinePayload.c_str(),
                onlinePayload.length(),
                1,
                1,
                true);
        }
        if (!bootstrapPublished)
        {
            bootstrapRequested = true;
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        connected = false;
        subscribed = false;
        pendingTelemetryMessageId = -1;
        pendingBootstrapMessageId = -1;
        inFlightFile = "";
        lastStatus = "DISCONNECTED";
        Serial.println("[MQTT] Disconnected");
        markBackfillRequested = true;
        scheduleReconnect();
        break;

    case MQTT_EVENT_DATA:
        collectIncomingData(event);
        break;

    case MQTT_EVENT_PUBLISHED:
        if (event->msg_id == pendingBootstrapMessageId)
        {
            pendingBootstrapMessageId = -1;
            bootstrapRequested = false;
            bootstrapPublished = true;
            lastStatus = "BOOTSTRAP_PUBACK_RECEIVED";
            break;
        }
        if (event->msg_id == pendingTelemetryMessageId)
        {
            telemetryAcknowledge(inFlightFile);
            pendingTelemetryMessageId = -1;
            inFlightFile = "";
            lastStatus = "PUBACK_RECEIVED";
        }
        break;

    case MQTT_EVENT_ERROR:
        lastStatus = "TLS_OR_MQTT_ERROR";
        if (event->error_handle != nullptr)
        {
            Serial.printf(
                "[MQTT] Error tls=%d stack=%d verify=0x%x socket=%d\n",
                event->error_handle->esp_tls_last_esp_err,
                event->error_handle->esp_tls_stack_err,
                event->error_handle->esp_tls_cert_verify_flags,
                event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        break;
    }

    return ESP_OK;
}

void startClientWhenReady()
{
    if (clientStarted || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (settingsGet().mqttHost == "mqtt.example.com" || String(MQTT_CA_CERT).isEmpty())
    {
        lastStatus = "MQTT_CONFIG_REQUIRED";
        return;
    }

    if (getUnixTimestamp() == 0)
    {
        lastStatus = "WAITING_FOR_NTP";
        return;
    }

    esp_mqtt_client_start(client);
    clientStarted = true;
    lastStatus = "CONNECTING";
}

void reconnectWhenDue()
{
    if (!clientStarted || connected || WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (millis() < nextReconnectMillis)
    {
        return;
    }

    esp_mqtt_client_reconnect(client);
    lastStatus = "RECONNECTING";
    scheduleReconnect();
}

void publishOldestQueuedBatch()
{
    if (!connected || !subscribed || !anyChannelRunning() ||
        pendingTelemetryMessageId >= 0 || pendingBootstrapMessageId >= 0)
    {
        return;
    }

    String path;
    String payload;
    if (!telemetryPeekOldest(path, payload))
    {
        return;
    }

    const int messageId = esp_mqtt_client_enqueue(
        client,
        telemetryTopic.c_str(),
        payload.c_str(),
        payload.length(),
        1,
        0,
        true);

    if (messageId >= 0)
    {
        inFlightFile = path;
        pendingTelemetryMessageId = messageId;
        lastStatus = "WAITING_FOR_PUBACK";
    }
    else
    {
        lastStatus = "PUBLISH_FAILED";
    }
}

void publishBootstrapWhenRequested()
{
    if (!bootstrapRequested || bootstrapPublished || !connected || !subscribed ||
        pendingBootstrapMessageId >= 0 || pendingTelemetryMessageId >= 0)
    {
        return;
    }

    if (!settingsTestInformationComplete())
    {
        lastStatus = "WAITING_FOR_TEST_INFO";
        return;
    }

    batterySampleAll();
    String payload;
    if (!telemetryBuildBootstrapPayload(payload))
    {
        lastStatus = "BOOTSTRAP_BUILD_FAILED";
        return;
    }

    pendingBootstrapMessageId = esp_mqtt_client_enqueue(
        client,
        telemetryTopic.c_str(),
        payload.c_str(),
        payload.length(),
        1,
        0,
        true);

    lastStatus = pendingBootstrapMessageId >= 0
                     ? "WAITING_FOR_BOOTSTRAP_PUBACK"
                     : "BOOTSTRAP_PUBLISH_FAILED";
}

void manageOfflineBuffer()
{
    const unsigned long now = millis();
    const bool mqttOnline =
        WiFi.status() == WL_CONNECTED && connected && subscribed;

    if (mqttOnline)
    {
        hasBeenMqttOnline = true;
        if (offlineBuffering)
        {
            const unsigned long offlineDuration = now - offlineStartedMillis;
            if (offlineDuration >= OFFLINE_HOURLY_COMPACTION_DELAY_MS)
            {
                const uint16_t compacted = telemetryCompactBackfillToHourly();
                Serial.printf(
                    "[MQTT] Reconnected after %lu s, compacted %u hourly batches\n",
                    offlineDuration / 1000,
                    compacted);
            }
            else
            {
                Serial.printf(
                    "[MQTT] Reconnected after %lu s, keeping 10-minute backlog\n",
                    offlineDuration / 1000);
            }
            offlineBuffering = false;
        }
        return;
    }

    if (!hasBeenMqttOnline)
    {
        return;
    }

    if (!offlineBuffering)
    {
        offlineBuffering = true;
        offlineStartedMillis = now;
        lastCompactionCheckMillis = now;
        telemetryMarkQueuedAsBackfill();
        Serial.println("[MQTT] Offline buffering started");
    }

    const unsigned long offlineDuration = now - offlineStartedMillis;
    if (offlineDuration < OFFLINE_HOURLY_COMPACTION_DELAY_MS ||
        now - lastCompactionCheckMillis < OFFLINE_COMPACTION_CHECK_MS)
    {
        return;
    }

    lastCompactionCheckMillis = now;
    const uint16_t compacted = telemetryCompactBackfillToHourly();
    if (compacted > 0)
    {
        Serial.printf(
            "[MQTT] Offline %lu s, compacted %u hourly batches\n",
            offlineDuration / 1000,
            compacted);
    }
}
} // namespace

void mqttBegin()
{
    telemetryTopic = "id100/" + getGatewayId() + "/telemetry";
    desiredTopic = "id100/" + getGatewayId() + "/+/desired";
    statusTopic = "id100/" + getGatewayId() + "/status";
    brokerUri = "mqtts://" + settingsGet().mqttHost + ":" + String(MQTT_PORT);
    lastWillPayload = gatewayStatusPayload("offline");

    esp_mqtt_client_config_t config = {};
    config.event_handle = mqttEventHandler;
    config.uri = brokerUri.c_str();
    config.client_id = getGatewayId().c_str();
    config.username = settingsGet().mqttUsername.c_str();
    config.password = settingsGet().mqttPassword.c_str();
    config.cert_pem = MQTT_CA_CERT;
    config.keepalive = MQTT_KEEP_ALIVE_SECONDS;
    config.disable_clean_session = true;
    config.disable_auto_reconnect = true;
    config.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    config.buffer_size = 4096;
    config.out_buffer_size = 4096;
    config.lwt_topic = statusTopic.c_str();
    config.lwt_msg = lastWillPayload.c_str();
    config.lwt_qos = 1;
    config.lwt_retain = true;

    client = esp_mqtt_client_init(&config);
    if (client == nullptr)
    {
        lastStatus = "INIT_FAILED";
    }
}

void mqttLoop()
{
    if (client == nullptr)
    {
        return;
    }

    if (markBackfillRequested)
    {
        markBackfillRequested = false;
        telemetryMarkQueuedAsBackfill();
    }

    manageOfflineBuffer();

    startClientWhenReady();
    reconnectWhenDue();
    publishBootstrapWhenRequested();
    publishOldestQueuedBatch();
}

bool mqttQueueMeasurement(uint8_t channelMask, uint16_t aggregationIntervalSeconds)
{
    const bool isBackfill =
        WiFi.status() != WL_CONNECTED || !connected || !subscribed;
    return telemetryCaptureAndStore(
        channelMask,
        isBackfill,
        aggregationIntervalSeconds);
}

bool mqttIsConnected()
{
    return connected && subscribed;
}

String mqttLastStatus()
{
    return lastStatus;
}

const String &mqttDesiredTopic()
{
    return desiredTopic;
}
