#pragma once

#include <Arduino.h>

enum class ChannelState : uint8_t
{
    Idle,
    Running,
    Paused
};

struct CommandResponse
{
    bool valid;
    String topic;
    String payload;
};

void commandBegin();
CommandResponse commandHandle(const String &topic, const String &payload);
ChannelState commandChannelState(uint8_t channel);
uint8_t commandConsumeImmediateSampleMask();
uint32_t commandNextSequence(uint8_t channel);
