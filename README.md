# ID100 Battery Monitor

ESP32-C3 firmware for monitoring ID100 battery voltage, with four ADC channels,
configuration through a local Web AP, and server communication over MQTT with TLS.

The system performs two main tasks:

- Measure and display all four channels in real time on the local Web AP.
- When the server starts a test on a channel, sample every second, calculate
  10-minute averages, and publish telemetry over MQTT with QoS 1.

If Wi-Fi or MQTT disconnects, channels in the `running` state continue measuring.
Completed telemetry that cannot be delivered is stored in LittleFS and sent as
backfill when the connection is restored.

## Hardware

| Channel | ADC pin | Default name |
|---|---:|---|
| CH1 | GPIO1 | ID-100 Unit 01 |
| CH2 | GPIO2 | ID-100 Unit 02 |
| CH3 | GPIO3 | ID-100 Unit 03 |
| CH4 | GPIO4 | ID-100 Unit 04 |

Inputs must use a suitable voltage divider and share GND with the ESP32-C3. Do not
apply voltages above the ADC input limit directly to a GPIO pin.

## Battery Measurement

### ADC Readings

For each channel measurement, the firmware performs 32 iterations of
`analogRead()` and `analogReadMilliVolts()`, spaced 150 microseconds apart, then
averages the readings. The ADC uses 12-bit resolution and `ADC_11db` attenuation.

```cpp
#define ADC_SAMPLES 32
#define ADC_SAMPLE_DELAY_US 150
#define ADC_SAMPLE_INTERVAL_MS 1000UL
```

### Voltage Conversion

For the voltage divider using 5.6 MΩ and 1.1 MΩ resistors, battery voltage is
calibrated using:

```text
battery_voltage_mv = adc_voltage_mv × 6.11788007744669
                     + 26.958024845082
```

Battery percentage is linearly interpolated from 2,400 mV (0%) to 3,300 mV (100%)
and clamped to the range 0–100%.

| Battery voltage | Web AP status |
|---:|---|
| Below 500 mV | `not_connected` |
| 500–2,400 mV | `empty` |
| Above 2,400 and below 2,500 mV | `low` |
| 2,500 to below 3,300 mV | `normal` |
| 3,300 to 3,450 mV | `full` |
| Above 3,450 mV | `over_voltage` |

`battery_status` is not included in telemetry; the server evaluates battery status
from the voltage or percentage data.

## Sampling and 10-Minute Averages

Each channel has an independent state: `idle`, `running`, or `paused`.

While a channel is `running`:

1. Produce one sample every second by averaging 32 ADC readings.
2. Accumulate 60 samples to produce a one-minute average.
3. Accumulate 10 one-minute results to produce a 10-minute average.
4. Write telemetry to LittleFS before attempting to publish it over MQTT.
5. Delete the file only after the broker returns PUBACK.

The firmware keeps only sums and counters in RAM, rather than an array of 600
samples. RAM usage for averaging therefore does not grow over time.

Immediately after a `start` command, the firmware takes a measurement and creates
the first telemetry record with a one-second aggregation period. That sample also
contributes to the first one-minute average.

While `idle` or `paused`, the ADC is still read every second for the Web AP's live
display, but readings do not contribute to averages and are not sent to the server.
`pause` preserves the accumulated sums; `resume` collects the remaining samples,
excluding time spent paused. `stop` clears the incomplete averages held in RAM.

## Device Identity

A stable `gateway_id` is generated from the Wi-Fi STA MAC address:

```text
GW-{12 uppercase MAC characters}
```

Example:

```text
Gateway: GW-80B54E1FDF14
CH1:     GW-80B54E1FDF14-CH1
CH2:     GW-80B54E1FDF14-CH2
CH3:     GW-80B54E1FDF14-CH3
CH4:     GW-80B54E1FDF14-CH4
```

A new `boot_id` UUID is generated on every boot. Each channel's state, `session_id`,
and `sequence_no` are stored in NVS and restored after a reset or power loss.
Sequence numbers increment independently for each channel, so CH1–CH4 do not need
to have matching values.

## Web AP

The ESP always broadcasts a configuration network:

```text
SSID:     ID100-Battery-Monitor
Password: 12345678
Web:      http://4.4.4.4
```

The AP runs alongside the router connection using `WIFI_AP_STA`. Wi-Fi sleep is
disabled; the firmware checks the AP every five seconds and restarts it if needed.

The Web AP lets you:

- Enter `Gateway name`, `Tester name`, and `Test location`.
- Scan for and select a Wi-Fi router, then enter its password.
- View the connected SSID, IP address, and RSSI.
- View MQTT status, channel states, raw ADC readings, ADC voltage in mV, battery
  voltage, and battery percentage.

All three Test Information fields are required before `start`. Test information
and Wi-Fi settings are stored in NVS. While a phone is connected to the AP, the
firmware does not automatically retry the router connection, to keep the AP
stable. When no AP clients are connected, the STA retries the saved Wi-Fi network
every 60 seconds.

## MQTT

| Property | Value |
|---|---|
| Protocol | MQTT 3.1.1 |
| Transport | TLS, port 8883 |
| Client ID | `gateway_id` |
| QoS | 1 |
| Keep Alive | 30 seconds |
| Clean session | Disabled |
| Reconnect | Managed by the firmware, 1–30 seconds |

The broker settings, username, password, and CA certificate are in `src/config.h`.
Production credentials should not be committed to a public repository; move them
to build-time secrets or a configuration file excluded from Git before publishing.

### Topics

| Purpose | Topic |
|---|---|
| Gateway status | `id100/{gateway_id}/status` |
| Telemetry | `id100/{gateway_id}/telemetry` |
| Server command | `id100/{gateway_id}/+/desired` |
| Command ACK | `id100/{gateway_id}/{device_id}/ack` |

### Connection Flow

1. The ESP connects to the Wi-Fi router and synchronizes time via NTP.
2. MQTT connects over TLS and subscribes to `desired` with QoS 1.
3. After SUBACK, the ESP publishes `online` with QoS 1 and retain set to `true`.
4. The ESP measures and publishes bootstrap telemetry for all channels CH1–CH4,
   including idle channels.
5. Only after the bootstrap PUBACK does the ESP send queued telemetry in order.

Bootstrap telemetry lets the server identify the gateway and all four channels
after startup. It is created only when Test Information is complete.

Online payload:

```json
{
  "schema_version": 1,
  "type": "gateway_status",
  "gateway_id": "GW-80B54E1FDF14",
  "status": "online",
  "gateway_name": "Skylink test 01",
  "firmware_version": "0.1.0-demo",
  "boot_id": "current-boot-uuid"
}
```

## Last Will

The Last Will is configured before connecting:

- Topic: `id100/{gateway_id}/status`.
- QoS 1, retain set to `true`.
- Keep Alive: 30 seconds.

```json
{
  "schema_version": 1,
  "type": "gateway_status",
  "gateway_id": "GW-80B54E1FDF14",
  "status": "offline",
  "reason": "connection_lost"
}
```

If the ESP loses power or network connectivity without disconnecting cleanly, the
broker publishes the Last Will. Offline notification timing depends on the broker
and network and is not necessarily exactly 30 seconds. On reconnection, the
retained online message replaces the retained offline message.

## Commands and ACKs

| Action | desired_state | Condition |
|---|---|---|
| `start` | `running` | Channel is idle and Test Information is complete |
| `pause` | `paused` | Channel is running |
| `resume` | `running` | Channel is paused and `session_id` matches |
| `stop` | `stopped` | Returns the channel to idle |

Example: start CH1:

```json
{
  "schema_version": 1,
  "type": "test_command",
  "command_id": "test-start-ch1-001",
  "gateway_id": "GW-80B54E1FDF14",
  "device_id": "GW-80B54E1FDF14-CH1",
  "channel": 1,
  "action": "start",
  "desired_state": "running",
  "session_id": "session-001"
}
```

A successful ACK contains `result: "ok"`. An error ACK contains `result: "error"`,
`error_code`, and `error_message`. Error codes include `INVALID_COMMAND_ID`,
`INVALID_TYPE`, `INVALID_GATEWAY`, `INVALID_DEVICE`, `CONFIG_REQUIRED`,
`INVALID_STATE`, `INVALID_SESSION`, and `INVALID_ACTION`.

The firmware caches the 10 most recent ACKs. If the server resends the same
`command_id`, the ESP returns the cached ACK without executing the action again.

## Telemetry Payload

```json
{
  "schema_version": 1,
  "type": "telemetry_batch",
  "gateway_id": "GW-80B54E1FDF14",
  "gateway_name": "Skylink test 01",
  "firmware_version": "0.1.0-demo",
  "boot_id": "current-boot-uuid",
  "tester_name": "Skylink_Tomn",
  "test_location": "ID-100 Test Lab",
  "measurements": [
    {
      "device_id": "GW-80B54E1FDF14-CH1",
      "channel": 1,
      "sequence_no": 25,
      "captured_at": "2026-09-05T04:10:00.000Z",
      "adc_raw": 700,
      "adc_voltage_mv": 513,
      "battery_voltage_mv": 3165,
      "battery_percent": 85.0,
      "wifi_rssi": -58,
      "uptime_s": 650,
      "is_backfill": false
    }
  ]
}
```

A batch can contain one or more measurements. The payload includes neither `label`
nor `battery_status`.

## Offline Buffer and Backfill

Each telemetry record is written to LittleFS before publishing:

```text
Create telemetry → write to LittleFS → publish with QoS 1 → PUBACK → delete file
```

When the network is unavailable, files are retained and measurements are marked
with `is_backfill: true`. After reconnection, the ESP sends records from oldest to
newest. The queue is drained only while at least one channel is `running`.

During the first three hours offline, the firmware keeps 10-minute records. From
three hours onward, every six 10-minute records with the same set of channels are
merged into a one-hour average record. The firmware checks for compaction every
60 seconds. Compaction metadata is used internally and removed before sending,
so the server schema remains unchanged.

The queue is limited to 200 files; exceeding this limit deletes the oldest file.
The `stop` command removes unsent measurements for the specified `device_id`,
while preserving other channels' data in the same file.

LittleFS survives resets and power loss, but incomplete sums that have not yet
formed a 10-minute telemetry record exist only in RAM and are lost when power is
removed. Normal firmware uploads preserve NVS and LittleFS; a full flash erase
removes Wi-Fi settings, Test Information, channel states, and queued telemetry.

## Source Structure

| File | Purpose |
|---|---|
| `src/main.cpp` | Initialization and main loop |
| `src/config.h` | GPIO, ADC, battery thresholds, MQTT, and timing |
| `src/battery_manager.*` | ADC readings and battery data conversion |
| `src/sampling_manager.*` | Sampling and 1-minute/10-minute averages |
| `src/identity_manager.*` | Gateway ID, device ID, and boot ID |
| `src/settings_manager.*` | NVS storage for test, Wi-Fi, and MQTT settings |
| `src/command_manager.*` | State machine, validation, and ACKs |
| `src/telemetry_manager.*` | Payloads, LittleFS queue, and backfill |
| `src/mqtt_manager.*` | MQTT over TLS, Last Will, reconnection, and PUBACK |
| `src/ap_web_manager.*` | Web AP and configuration/monitoring API |
| `src/time_manager.*` | NTP and UTC timestamps |
