#include "command_manager.h"

#include "config.h"
#include "identity_manager.h"
#include "settings_manager.h"
#include "telemetry_manager.h"
#include "time_manager.h"

#include <ArduinoJson.h>
#include <Preferences.h>

namespace
{
struct ChannelContext
{
    ChannelState state = ChannelState::Idle;
    String sessionId;
    uint32_t sequence = 0;
};

ChannelContext channels[ADC_CHANNEL_COUNT];
Preferences commandCache;
volatile uint8_t immediateSampleMask = 0;
constexpr uint8_t COMMAND_CACHE_SIZE = 10;

String deviceIdForChannel(uint8_t channel)
{
    return getDeviceId(channel);
}

String ackTopic(const String &deviceId)
{
    return "id100/" + getGatewayId() + "/" + deviceId + "/ack";
}

String makeAck(
    const String &commandId,
    const String &deviceId,
    const String &result,
    const String &errorCode = "",
    const String &errorMessage = "")
{
    JsonDocument ack;
    ack["schema_version"] = SCHEMA_VERSION;
    ack["type"] = "command_ack";
    ack["command_id"] = commandId;
    ack["gateway_id"] = getGatewayId();
    ack["device_id"] = deviceId;
    ack["result"] = result;

    if (result == "error")
    {
        ack["error_code"] = errorCode;
        ack["error_message"] = errorMessage;
    }

    ack["device_time_utc"] = getUtcIsoTimestamp();
    String payload;
    serializeJson(ack, payload);
    return payload;
}

String cacheKey(uint8_t slot, const char *suffix)
{
    return String(suffix) + String(slot);
}

bool findCachedAck(const String &commandId, String &ack)
{
    for (uint8_t slot = 0; slot < COMMAND_CACHE_SIZE; slot++)
    {
        if (commandCache.getString(cacheKey(slot, "id").c_str(), "") == commandId)
        {
            ack = commandCache.getString(cacheKey(slot, "ack").c_str(), "");
            return true;
        }
    }
    return false;
}

void cacheAck(const String &commandId, const String &ack)
{
    const uint8_t slot = commandCache.getUChar("next", 0);
    commandCache.putString(cacheKey(slot, "id").c_str(), commandId);
    commandCache.putString(cacheKey(slot, "ack").c_str(), ack);
    commandCache.putUChar("next", (slot + 1) % COMMAND_CACHE_SIZE);
}

CommandResponse errorResponse(
    const String &commandId,
    const String &deviceId,
    const String &code,
    const String &message)
{
    return {
        true,
        ackTopic(deviceId),
        makeAck(commandId, deviceId, "error", code, message),
    };
}
} // namespace

void commandBegin()
{
    commandCache.begin("command_cache", false);
}

CommandResponse commandHandle(const String &topic, const String &payload)
{
    JsonDocument command;
    if (deserializeJson(command, payload))
    {
        return {false, "", ""};
    }

    const String commandId = command["command_id"] | "";
    const String deviceId = command["device_id"] | "";
    const int channelNumber = command["channel"] | 0;
    const int channel = channelNumber - 1;

    if (commandId.isEmpty())
    {
        return errorResponse(commandId, deviceId, "INVALID_COMMAND_ID", "command_id is required");
    }

    if (command["type"] != "test_command")
    {
        return errorResponse(commandId, deviceId, "INVALID_TYPE", "type must be test_command");
    }

    const String requestedGatewayId = command["gateway_id"] | "";
    if (requestedGatewayId != getGatewayId())
    {
        return errorResponse(commandId, deviceId, "INVALID_GATEWAY", "gateway_id does not match");
    }

    if (channel < 0 || channel >= ADC_CHANNEL_COUNT ||
        deviceId != deviceIdForChannel(channel))
    {
        return errorResponse(commandId, deviceId, "INVALID_DEVICE", "device_id or channel is invalid");
    }

    String cachedAck;
    if (findCachedAck(commandId, cachedAck))
    {
        return {
            true,
            ackTopic(deviceId),
            cachedAck,
        };
    }

    ChannelContext &context = channels[channel];
    const String action = command["action"] | "";
    const String desiredState = command["desired_state"] | "";
    String errorCode;
    String errorMessage;

    if (action == "start" && desiredState == "running")
    {
        if (!settingsTestInformationComplete())
        {
            errorCode = "CONFIG_REQUIRED";
            errorMessage = "Gateway name, tester name and test location are required";
        }
        else if (context.state != ChannelState::Idle)
        {
            errorCode = "INVALID_STATE";
            errorMessage = "Start requires idle state";
        }
        else
        {
            context.state = ChannelState::Running;
            context.sessionId = command["session_id"] | "";
            immediateSampleMask |= (1U << channel);
        }
    }
    else if (action == "pause" && desiredState == "paused")
    {
        if (context.state != ChannelState::Running)
        {
            errorCode = "INVALID_STATE";
            errorMessage = "Pause requires running state";
        }
        else
        {
            context.state = ChannelState::Paused;
        }
    }
    else if (action == "resume" && desiredState == "running")
    {
        const String requestedSessionId = command["session_id"] | "";
        if (context.state != ChannelState::Paused)
        {
            errorCode = "INVALID_STATE";
            errorMessage = "Resume requires paused state";
        }
        else if (requestedSessionId != context.sessionId)
        {
            errorCode = "INVALID_SESSION";
            errorMessage = "Resume must use the active session_id";
        }
        else
        {
            context.state = ChannelState::Running;
        }
    }
    else if (action == "stop" && desiredState == "stopped")
    {
        context.state = ChannelState::Idle;
        telemetryDiscardDevice(deviceId);
    }
    else
    {
        errorCode = "INVALID_ACTION";
        errorMessage = "action or desired_state is invalid";
    }

    const String ack = errorCode.isEmpty()
                           ? makeAck(commandId, deviceId, "ok")
                           : makeAck(commandId, deviceId, "error", errorCode, errorMessage);
    cacheAck(commandId, ack);
    return {true, ackTopic(deviceId), ack};
}

ChannelState commandChannelState(uint8_t channel)
{
    return channel < ADC_CHANNEL_COUNT ? channels[channel].state : ChannelState::Idle;
}

uint8_t commandConsumeImmediateSampleMask()
{
    const uint8_t mask = immediateSampleMask;
    immediateSampleMask = 0;
    return mask;
}

uint32_t commandNextSequence(uint8_t channel)
{
    if (channel >= ADC_CHANNEL_COUNT)
    {
        return 0;
    }

    return ++channels[channel].sequence;
}
