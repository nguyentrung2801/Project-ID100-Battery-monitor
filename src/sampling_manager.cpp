#include "sampling_manager.h"

#include "battery_manager.h"
#include "command_manager.h"
#include "config.h"
#include "mqtt_manager.h"
#include "time_manager.h"

namespace
{
struct SampleView
{
    bool valid = false;
    uint16_t adcRaw = 0;
    uint16_t adcVoltageMv = 0;
    uint16_t batteryVoltageMv = 0;
    float batteryPercent = 0;
    String status;
};

struct SamplingAccumulator
{
    uint64_t secondRawTotal = 0;
    uint64_t secondVoltageTotal = 0;
    uint8_t secondSampleCount = 0;
    uint64_t minuteRawTotal = 0;
    uint64_t minuteVoltageTotal = 0;
    uint8_t minuteAverageCount = 0;
    unsigned long lastSampleMillis = 0;
    SampleView oneSecond;
    SampleView oneMinute;
    SampleView tenMinutes;
};

SamplingAccumulator accumulators[ADC_CHANNEL_COUNT];
ChannelState previousStates[ADC_CHANNEL_COUNT];
float displayedPercents[ADC_CHANNEL_COUNT] = {};
bool displayedPercentValid[ADC_CHANNEL_COUNT] = {};
uint8_t firstSamplePendingMask = 0;

SampleView makeView(const BatteryReading &reading)
{
    SampleView view;
    view.valid = true;
    view.adcRaw = reading.adcRaw;
    view.adcVoltageMv = reading.adcVoltageMv;
    view.batteryVoltageMv = reading.batteryVoltageMv;
    view.batteryPercent = reading.percent;
    view.status = reading.status;
    return view;
}

SampleView makeAveragedView(uint8_t channel, uint16_t raw, uint16_t voltage)
{
    const BatteryReading original = batteryGet(channel);
    batteryApplyAveragedSample(channel, raw, voltage);
    const SampleView view = makeView(batteryGet(channel));
    batteryApplyAveragedSample(channel, original.adcRaw, original.adcVoltageMv);
    return view;
}

void resetAccumulator(uint8_t channel)
{
    accumulators[channel] = SamplingAccumulator{};
}

void stopAccumulator(uint8_t channel)
{
    SamplingAccumulator &accumulator = accumulators[channel];
    accumulator.secondRawTotal = 0;
    accumulator.secondVoltageTotal = 0;
    accumulator.secondSampleCount = 0;
    accumulator.minuteRawTotal = 0;
    accumulator.minuteVoltageTotal = 0;
    accumulator.minuteAverageCount = 0;
    accumulator.lastSampleMillis = 0;
}

void processStateChanges()
{
    const uint8_t startedMask = commandConsumeImmediateSampleMask();
    firstSamplePendingMask |= startedMask;

    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        const ChannelState current = commandChannelState(channel);
        if ((startedMask & (1U << channel)) != 0)
        {
            resetAccumulator(channel);
            // Make the first ADC reading immediately after a server Start command.
            accumulators[channel].lastSampleMillis = millis() - ADC_SAMPLE_INTERVAL_MS;
        }
        if (current == ChannelState::Idle && previousStates[channel] != ChannelState::Idle)
        {
            stopAccumulator(channel);
            firstSamplePendingMask &= ~(1U << channel);
        }
        previousStates[channel] = current;
    }
}

void takeSample(
    uint8_t channel,
    uint8_t &immediatePublishMask,
    uint8_t &periodicPublishMask)
{
    SamplingAccumulator &accumulator = accumulators[channel];
    const unsigned long now = millis();
    if (now - accumulator.lastSampleMillis < ADC_SAMPLE_INTERVAL_MS)
    {
        return;
    }

    accumulator.lastSampleMillis = now;
    batterySampleChannel(channel);
    const BatteryReading &reading = batteryGet(channel);
    accumulator.oneSecond = makeView(reading);
    accumulator.secondRawTotal += reading.adcRaw;
    accumulator.secondVoltageTotal += reading.adcVoltageMv;
    accumulator.secondSampleCount++;

    if ((firstSamplePendingMask & (1U << channel)) != 0)
    {
        firstSamplePendingMask &= ~(1U << channel);
        displayedPercents[channel] = accumulator.oneSecond.batteryPercent;
        displayedPercentValid[channel] = true;
        immediatePublishMask |= (1U << channel);
    }

    if (accumulator.secondSampleCount < SAMPLES_PER_MINUTE)
    {
        return;
    }

    const uint16_t minuteRaw = accumulator.secondRawTotal / accumulator.secondSampleCount;
    const uint16_t minuteVoltage = accumulator.secondVoltageTotal / accumulator.secondSampleCount;
    accumulator.secondRawTotal = 0;
    accumulator.secondVoltageTotal = 0;
    accumulator.secondSampleCount = 0;
    accumulator.oneMinute = makeAveragedView(channel, minuteRaw, minuteVoltage);
    accumulator.minuteRawTotal += minuteRaw;
    accumulator.minuteVoltageTotal += minuteVoltage;
    accumulator.minuteAverageCount++;

    if (accumulator.minuteAverageCount < MINUTE_AVERAGES_PER_TELEMETRY)
    {
        return;
    }

    const uint16_t tenMinuteRaw = accumulator.minuteRawTotal / accumulator.minuteAverageCount;
    const uint16_t tenMinuteVoltage = accumulator.minuteVoltageTotal / accumulator.minuteAverageCount;
    accumulator.minuteRawTotal = 0;
    accumulator.minuteVoltageTotal = 0;
    accumulator.minuteAverageCount = 0;
    batteryApplyAveragedSample(channel, tenMinuteRaw, tenMinuteVoltage);
    accumulator.tenMinutes = makeView(batteryGet(channel));
    displayedPercents[channel] = accumulator.tenMinutes.batteryPercent;
    displayedPercentValid[channel] = true;
    periodicPublishMask |= (1U << channel);
}

void takeLiveDisplaySample(uint8_t channel)
{
    SamplingAccumulator &accumulator = accumulators[channel];
    const unsigned long now = millis();
    if (now - accumulator.lastSampleMillis < ADC_SAMPLE_INTERVAL_MS)
    {
        return;
    }

    // Keep the Web AP live while the test is idle or paused. These readings
    // are deliberately excluded from all one-minute/ten-minute accumulators
    // and are never queued as MQTT telemetry.
    accumulator.lastSampleMillis = now;
    batterySampleChannel(channel);
    accumulator.oneSecond = makeView(batteryGet(channel));
    if (!displayedPercentValid[channel])
    {
        displayedPercents[channel] = accumulator.oneSecond.batteryPercent;
        displayedPercentValid[channel] = true;
    }
}

} // namespace

void samplingBegin()
{
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        previousStates[channel] = commandChannelState(channel);
    }
}

void samplingLoop()
{
    processStateChanges();
    uint8_t immediatePublishMask = 0;
    uint8_t periodicPublishMask = 0;
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        if (commandChannelState(channel) == ChannelState::Running)
        {
            takeSample(channel, immediatePublishMask, periodicPublishMask);
        }
        else
        {
            takeLiveDisplaySample(channel);
        }
    }
    if (getUnixTimestamp() == 0)
    {
        return;
    }
    if (immediatePublishMask != 0)
    {
        mqttQueueMeasurement(immediatePublishMask, 1);
    }
    if (periodicPublishMask != 0)
    {
        mqttQueueMeasurement(
            periodicPublishMask,
            SAMPLES_PER_MINUTE * MINUTE_AVERAGES_PER_TELEMETRY);
    }
}

void samplingAddStatus(JsonDocument &document)
{
    JsonArray channels = document["sampling_channels"].to<JsonArray>();
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        const SamplingAccumulator &accumulator = accumulators[channel];
        JsonObject item = channels.add<JsonObject>();
        item["channel"] = channel + 1;
        const ChannelState state = commandChannelState(channel);
        item["state"] = state == ChannelState::Running
                            ? "running"
                            : (state == ChannelState::Paused ? "paused" : "idle");
        item["displayed_battery_percent"] = displayedPercentValid[channel]
                                                  ? displayedPercents[channel]
                                                  : batteryGet(channel).percent;
    }
}
