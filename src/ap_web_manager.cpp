#include "ap_web_manager.h"

#include "battery_manager.h"
#include "command_manager.h"
#include "config.h"
#include "identity_manager.h"
#include "mqtt_manager.h"
#include "sampling_manager.h"
#include "settings_manager.h"
#include "time_manager.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

namespace
{
constexpr uint16_t WEB_SERVER_PORT = 80;
constexpr uint8_t MAX_WIFI_NETWORKS = 20;

WebServer server(WEB_SERVER_PORT);

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ID100 Battery Monitor</title>
  <style>
    :root {
      --bg: #06101d;
      --card: #0f2033;
      --card2: #0a1928;
      --line: #203d59;
      --text: #f1f6fc;
      --muted: #8fa5bc;
      --blue: #2397ee;
      --green: #4fe0a0;
      --red: #ff7882;
      --amber: #ffc968;
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      min-height: 100vh;
      background: radial-gradient(circle at top, #102944 0, #06101d 34%, #050c16 100%);
      color: var(--text);
      font: 15px Inter, system-ui, Arial, sans-serif;
    }

    .page {
      max-width: 1040px;
      margin: auto;
      padding: 34px 20px 54px;
    }

    .header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 18px;
      margin-bottom: 24px;
      padding: 4px;
    }

    .header h1 {
      margin: 0 0 7px;
      font-size: 29px;
      letter-spacing: -.6px;
    }

    .sub,
    .muted {
      color: var(--muted);
    }

    .pill {
      padding: 11px 18px;
      border: 1px solid #244866;
      border-radius: 999px;
      background: #132d48;
      font-weight: 850;
      letter-spacing: .5px;
    }

    .online { color: var(--green); }
    .offline { color: var(--red); }

    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 18px;
    }

    .card {
      padding: 23px;
      border: 1px solid var(--line);
      border-radius: 20px;
      background: linear-gradient(145deg, var(--card), #0c1a2b);
      box-shadow: 0 14px 35px rgba(0, 0, 0, .18);
    }

    .wide { grid-column: 1 / -1; }

    .battery-head {
      display: flex;
      align-items: flex-start;
      justify-content: space-between;
      gap: 14px;
    }

    .battery-head h2 {
      margin: 0;
      font-size: 21px;
      letter-spacing: .2px;
    }

    .state {
      display: inline-block;
      margin-top: 9px;
      padding: 5px 10px;
      border-radius: 999px;
      background: #192f48;
      color: var(--muted);
      font-size: 11px;
      font-weight: 850;
      letter-spacing: .7px;
      text-transform: uppercase;
    }

    .state.running { color: var(--green); background: #123a35; }
    .state.paused { color: var(--amber); background: #3a3220; }

    .percent {
      color: var(--green);
      font-size: 39px;
      font-weight: 900;
      letter-spacing: -1px;
    }

    .bar {
      height: 9px;
      margin: 20px 0 22px;
      overflow: hidden;
      border-radius: 8px;
      background: #293e54;
    }

    .bar span {
      display: block;
      height: 100%;
      background: linear-gradient(90deg, var(--blue), var(--green));
      transition: width .4s ease;
    }

    .metrics {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 14px;
    }

    .metric {
      padding: 13px;
      border: 1px solid #1b344d;
      border-radius: 13px;
      background: var(--card2);
      color: var(--muted);
    }

    .metric b {
      display: block;
      margin-top: 5px;
      color: var(--text);
      font-size: 18px;
    }

    .title {
      margin: 0 0 18px;
      font-size: 15px;
      letter-spacing: 1.2px;
    }

    .form-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 14px;
    }

    label {
      display: block;
      margin-bottom: 7px;
      color: var(--muted);
    }

    input,
    select {
      width: 100%;
      height: 47px;
      padding: 0 14px;
      border: 1px solid #31506e;
      border-radius: 12px;
      outline: none;
      background: #081725;
      color: white;
      font: inherit;
    }

    input:focus,
    select:focus {
      border-color: var(--blue);
      box-shadow: 0 0 0 3px rgba(35, 151, 238, .12);
    }

    button {
      height: 47px;
      padding: 0 22px;
      border: 0;
      border-radius: 12px;
      background: linear-gradient(135deg, #258fe0, #1ba6ed);
      color: #fff;
      font-weight: 800;
      cursor: pointer;
    }

    .actions {
      display: flex;
      align-items: center;
      flex-wrap: wrap;
      gap: 12px;
      margin-top: 16px;
    }

    .message {
      min-height: 20px;
      color: var(--green);
    }

    .message.error { color: var(--red); }

    .wifi-connection {
      margin: 0 0 6px;
      padding: 0;
      border: 0;
      border-radius: 0;
      background: none;
      color: var(--green);
      font-weight: 800;
    }

    .wifi-help {
      margin: 0 0 17px;
      color: var(--muted);
      font-size: 13px;
    }

    .wifi-row {
      display: grid;
      grid-template-columns: 1.3fr 1fr auto;
      align-items: end;
      gap: 12px;
    }

    .scan-state {
      margin-top: 11px;
      color: var(--muted);
      font-size: 13px;
    }

    .password-wrap { position: relative; }
    .password-wrap input { padding-right: 48px; }

    .password-toggle {
      position: absolute;
      top: 4px;
      right: 4px;
      display: grid;
      width: 39px;
      height: 39px;
      padding: 0;
      place-items: center;
      border-radius: 9px;
      background: transparent;
      color: var(--muted);
    }

    .password-toggle:hover,
    .password-toggle.revealed {
      background: #12283c;
      color: var(--text);
    }

    .password-toggle svg {
      width: 21px;
      height: 21px;
      fill: none;
      stroke: currentColor;
      stroke-width: 1.8;
      stroke-linecap: round;
      stroke-linejoin: round;
    }

    @media (max-width: 720px) {
      .grid { grid-template-columns: 1fr; }
      .wide { grid-column: auto; }
      .form-grid,
      .wifi-row { grid-template-columns: 1fr; }
      .header { align-items: flex-start; }
      .header h1 { font-size: 24px; }
      .percent { font-size: 34px; }
      .card { padding: 19px; }
    }
  </style>
</head>
<body>
  <main class="page">
    <header class="header">
      <div>
        <h1>ID100 Battery Monitor</h1>
        <div class="sub" id="systemInfo">Loading device status...</div>
      </div>
      <div class="pill offline" id="onlineState">OFFLINE</div>
    </header>

    <section class="grid">
      <article class="card wide">
        <h2 class="title">TEST INFORMATION</h2>
        <div class="form-grid">
          <div>
            <label for="gatewayName">Gateway name</label>
            <input id="gatewayName" maxlength="60" required>
          </div>
          <div>
            <label for="tester">Tester name</label>
            <input id="tester" maxlength="60" required>
          </div>
          <div>
            <label for="location">Test location</label>
            <input id="location" maxlength="100" required>
          </div>
        </div>
        <div class="actions">
          <button id="saveTest">Save information</button>
          <span class="message" id="testMessage"></span>
        </div>
      </article>

      <article class="card wide">
        <h2 class="title">WI-FI SETUP</h2>
        <div class="wifi-connection" id="wifiConnection">Checking Wi-Fi connection...</div>
        <p class="wifi-help">
          To configure another Wi-Fi network, select it and enter its password below.
        </p>
        <div class="wifi-row">
          <div>
            <label for="ssid">Wi-Fi network</label>
            <select id="ssid">
              <option>Scanning nearby networks...</option>
            </select>
          </div>
          <div>
            <label for="password">Password</label>
            <div class="password-wrap">
              <input id="password" type="password">
              <button
                class="password-toggle"
                id="togglePassword"
                type="button"
                aria-label="Show password"
                aria-pressed="false"
              >
                <svg viewBox="0 0 24 24" aria-hidden="true">
                  <path d="M2.5 12s3.5-6 9.5-6 9.5 6 9.5 6-3.5 6-9.5 6-9.5-6-9.5-6Z"/>
                  <circle cx="12" cy="12" r="2.7"/>
                </svg>
              </button>
            </div>
          </div>
          <button id="connectWifi">Connect</button>
        </div>
        <div class="scan-state" id="wifiMessage"></div>
      </article>

      <article class="card" id="battery0"></article>
      <article class="card" id="battery1"></article>
      <article class="card" id="battery2"></article>
      <article class="card" id="battery3"></article>
    </section>
  </main>

  <script>
    const byId = id => document.getElementById(id);
    const escapeHtml = value => String(value ?? '').replace(
      /[&<>]/g,
      char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[char])
    );

    let wifiAttempt = null;
    let wifiMessageUntil = 0;

    function showWifiMessage(text, durationMs = 0) {
      byId('wifiMessage').textContent = text;
      wifiMessageUntil = durationMs ? Date.now() + durationMs : 0;
    }

    function batteryCard(battery, sampling) {
      const percent = Number(
        sampling?.displayed_battery_percent ?? battery.battery_percent
      ).toFixed(1);
      const state = sampling?.state || 'idle';

      return `
        <div class="battery-head">
          <div>
            <h2>${escapeHtml(battery.device_id)}</h2>
            <div class="muted">
              GPIO${battery.gpio} · ${escapeHtml(battery.device_name)}
            </div>
            <span class="state ${state}">${state}</span>
          </div>
          <div class="percent">${percent}%</div>
        </div>
        <div class="bar">
          <span style="width:${percent}%"></span>
        </div>
        <div class="metrics">
          <div class="metric">
            Battery voltage
            <b>${(battery.battery_voltage_mv / 1000).toFixed(3)} V</b>
          </div>
          <div class="metric">
            ADC voltage
            <b>${battery.adc_voltage_mv} mV</b>
          </div>
          <div class="metric">
            ADC raw
            <b>${battery.adc_raw}</b>
          </div>
          <div class="metric">
            Battery status
            <b>${escapeHtml(battery.status || '—')}</b>
          </div>
        </div>`;
    }

    async function refreshStatus() {
      try {
        const status = await fetch('/api/status').then(
          response => response.json()
        );

        status.batteries.forEach((battery, index) => {
          byId(`battery${index}`).innerHTML = batteryCard(
            battery,
            status.sampling_channels?.[index]
          );
        });

        const mqttOnline = status.mqtt_connected === true;
        const wifiOnline = status.esp_status === 'online';
        byId('onlineState').textContent = mqttOnline ? 'ONLINE' : 'OFFLINE';
        byId('onlineState').className = `pill ${mqttOnline ? 'online' : 'offline'}`;

        const states = (status.sampling_channels || [])
          .map(channel => `CH${channel.channel}: ${channel.state}`)
          .join(' · ');
        byId('systemInfo').textContent =
          `Firmware ${status.firmware_version} · ${status.time} · ${states}`;
        byId('wifiConnection').textContent = wifiOnline
          ? `Connected to ${status.sta_ssid} · ${status.sta_ip} · ${status.wifi_rssi} dBm`
          : 'Not connected to a Wi-Fi router.';

        if (wifiAttempt) {
          const now = Date.now();
          if (
            now - wifiAttempt.startedAt > 1500 &&
            wifiOnline &&
            status.sta_ssid === wifiAttempt.ssid
          ) {
            wifiAttempt = null;
            showWifiMessage('Connected successfully', 10000);
          } else if (now >= wifiAttempt.deadline) {
            wifiAttempt = null;
            showWifiMessage('Cannot connect to Wi-Fi', 10000);
          }
        } else if (wifiMessageUntil && Date.now() >= wifiMessageUntil) {
          showWifiMessage('');
        }
      } catch (error) {
        byId('onlineState').textContent = 'OFFLINE';
      }
    }

    async function loadConfig() {
      const config = await fetch('/api/config').then(
        response => response.json()
      );
      byId('gatewayName').value = config.gateway_name || '';
      byId('tester').value = config.tester_name || '';
      byId('location').value = config.test_location || '';
    }

    async function scanWifi() {
      const select = byId('ssid');
      if (!select.options.length || !select.value) {
        select.innerHTML =
          '<option value="">Scanning nearby networks...</option>';
      }

      try {
        const result = await fetch(
          '/api/wifi/scan',
          { cache: 'no-store' }
        ).then(response => response.json());

        if (result.scanning) {
          clearTimeout(window.wifiScanRetryTimer);
          window.wifiScanRetryTimer = setTimeout(scanWifi, 1000);
          return;
        }

        if (result.networks?.length) {
          const connectedSsid = result.connected_ssid || '';
          result.networks.sort((left, right) => {
            const leftConnected = connectedSsid && left.ssid === connectedSsid;
            const rightConnected = connectedSsid && right.ssid === connectedSsid;
            if (leftConnected !== rightConnected) {
              return leftConnected ? -1 : 1;
            }
            return Number(right.rssi) - Number(left.rssi);
          });

          select.innerHTML = result.networks
            .map(network => `
              <option value="${escapeHtml(network.ssid)}">
                ${escapeHtml(network.ssid)} · ${network.rssi} dBm${network.open ? ' · Open' : ''}
              </option>`)
            .join('');
          return;
        }
      } catch (error) {}

      select.innerHTML = '<option value="">No networks found</option>';
    }

    byId('saveTest').onclick = async () => {
      const gatewayName = byId('gatewayName').value.trim();
      const tester = byId('tester').value.trim();
      const location = byId('location').value.trim();
      const message = byId('testMessage');
      clearTimeout(window.testMessageTimer);

      if (!gatewayName || !tester || !location) {
        message.classList.add('error');
        message.textContent = 'All test information fields are required';
        return;
      }

      const body = new URLSearchParams({
        gateway_name: gatewayName,
        tester,
        location
      });
      const response = await fetch('/api/config', {
        method: 'POST',
        body
      });
      message.classList.toggle('error', !response.ok);
      message.textContent = await response.text();
      clearTimeout(window.testMessageTimer);
      window.testMessageTimer = setTimeout(
        () => message.textContent = '',
        10000
      );
    };

    byId('togglePassword').onclick = () => {
      const input = byId('password');
      const reveal = input.type === 'password';
      input.type = reveal ? 'text' : 'password';
      byId('togglePassword').classList.toggle('revealed', reveal);
      byId('togglePassword').setAttribute('aria-pressed', String(reveal));
      byId('togglePassword').setAttribute(
        'aria-label',
        reveal ? 'Hide password' : 'Show password'
      );
      input.focus();
    };

    byId('password').addEventListener('input', () => {
      const input = byId('password');
      if (input.type === 'text') {
        input.type = 'password';
        byId('togglePassword').classList.remove('revealed');
        byId('togglePassword').setAttribute('aria-pressed', 'false');
        byId('togglePassword').setAttribute('aria-label', 'Show password');
      }
    });

    byId('connectWifi').onclick = async () => {
      const ssid = byId('ssid').value;
      if (!ssid) {
        return;
      }

      const body = new URLSearchParams({
        ssid,
        password: byId('password').value
      });
      wifiAttempt = {
        ssid,
        startedAt: Date.now(),
        deadline: Date.now() + 10000
      };
      showWifiMessage('');

      try {
        const response = await fetch('/api/wifi', {
          method: 'POST',
          body
        });
        if (!response.ok) {
          wifiAttempt = null;
        }
      } catch (error) {
        wifiAttempt = null;
      }
    };

    loadConfig();
    scanWifi();
    refreshStatus();
    setInterval(refreshStatus, 1000);
  </script>
</body>
</html>
)HTML";

void sendJson(const JsonDocument &document)
{
    String response;
    serializeJson(document, response);
    server.send(200, "application/json", response);
}

void handleStatusRequest()
{
    JsonDocument document;
    deserializeJson(document, batteryDashboardJson());
    document["sta_ssid"] = WiFi.SSID();
    document["sta_ip"] = WiFi.localIP().toString();
    document["time"] = getTimestamp();
    document["gateway_id"] = getGatewayId();
    document["mqtt_connected"] = mqttIsConnected();
    document["mqtt_status"] = mqttLastStatus();
    document["mqtt_desired_topic"] = mqttDesiredTopic();
    bool testRunning = false;
    for (uint8_t channel = 0; channel < ADC_CHANNEL_COUNT; channel++)
    {
        testRunning = testRunning ||
                      commandChannelState(channel) == ChannelState::Running;
    }
    document["test_running"] = testRunning;
    samplingAddStatus(document);
    sendJson(document);
}

void handleConfigGetRequest()
{
    const AppSettings &settings = settingsGet();
    JsonDocument document;
    document["gateway_name"] = settings.gatewayName;
    document["tester_name"] = settings.testerName;
    document["test_location"] = settings.testLocation;
    sendJson(document);
}

void handleConfigPostRequest()
{
    AppSettings settings = settingsGet();
    settings.gatewayName = server.arg("gateway_name");
    settings.gatewayName.trim();
    settings.testerName = server.arg("tester");
    settings.testerName.trim();
    settings.testLocation = server.arg("location");
    settings.testLocation.trim();
    if (settings.gatewayName.isEmpty() ||
        settings.testerName.isEmpty() ||
        settings.testLocation.isEmpty())
    {
        server.send(400, "text/plain", "All test information fields are required");
        return;
    }
    settingsSave(settings);
    server.send(200, "text/plain", "Test information saved");
}

void handleWifiScanRequest()
{
    static uint8_t scanAttempt = 0;
    static unsigned long scanStartedMillis = 0;
    JsonDocument document;
    JsonArray networks = document["networks"].to<JsonArray>();
    if (WiFi.status() == WL_CONNECTED)
    {
        document["connected_ssid"] = WiFi.SSID();
    }
    int resultCount = WiFi.scanComplete();

    if (resultCount == WIFI_SCAN_RUNNING &&
        millis() - scanStartedMillis >= WIFI_SCAN_TIMEOUT_MS)
    {
        Serial.println("[WiFi] Scan timeout, restarting scan");
        WiFi.scanDelete();
        resultCount = WIFI_SCAN_FAILED;
    }

    if (resultCount == WIFI_SCAN_FAILED)
    {
        // A pending connection to an unavailable saved router can monopolize
        // the single ESP32-C3 radio. Stop STA association only; AP stays up.
        if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() > 0)
        {
            WiFi.disconnect(false, false);
            delay(30);
        }

        scanAttempt = max<uint8_t>(scanAttempt, 1);
        const int scanStartResult = WiFi.scanNetworks(true, true);
        scanStartedMillis = millis();
        document["scanning"] = scanStartResult == WIFI_SCAN_RUNNING;
        document["scan_status"] = scanStartResult;
        document["attempt"] = scanAttempt;
        sendJson(document);
        return;
    }

    if (resultCount == WIFI_SCAN_RUNNING)
    {
        document["scanning"] = true;
        document["attempt"] = scanAttempt;
        sendJson(document);
        return;
    }

    if (resultCount == 0 && scanAttempt < WIFI_SCAN_MAX_ATTEMPTS)
    {
        scanAttempt++;
        WiFi.scanDelete();
        delay(30);
        const int scanStartResult = WiFi.scanNetworks(true, true);
        scanStartedMillis = millis();
        document["scanning"] = scanStartResult == WIFI_SCAN_RUNNING;
        document["scan_status"] = scanStartResult;
        document["attempt"] = scanAttempt;
        sendJson(document);
        return;
    }

    document["scanning"] = false;
    document["scan_status"] = resultCount;
    document["attempt"] = scanAttempt;

    for (int index = 0; index < resultCount && index < MAX_WIFI_NETWORKS; index++)
    {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(index);
        network["rssi"] = WiFi.RSSI(index);
        network["open"] = WiFi.encryptionType(index) == WIFI_AUTH_OPEN;
    }

    WiFi.scanDelete();
    scanAttempt = 0;
    scanStartedMillis = 0;

    sendJson(document);
}

void handleWifiSaveRequest()
{
    const String ssid = server.arg("ssid");
    if (ssid.isEmpty())
    {
        server.send(400, "text/plain", "Please select a Wi-Fi network");
        return;
    }

    saveWifi(ssid, server.arg("password"));
    server.send(200, "text/plain", "Wi-Fi saved. ESP32 is connecting...");
}
} // namespace

void setupAPWebServer()
{
    server.on("/", []()
              { server.send_P(200, "text/html", INDEX_HTML); });
    server.on("/api/status", HTTP_GET, handleStatusRequest);
    server.on("/api/config", HTTP_GET, handleConfigGetRequest);
    server.on("/api/config", HTTP_POST, handleConfigPostRequest);
    server.on("/api/wifi/scan", HTTP_GET, handleWifiScanRequest);
    server.on("/api/wifi", HTTP_POST, handleWifiSaveRequest);
    server.begin();
}

void handleAPWebClient()
{
    server.handleClient();
}
