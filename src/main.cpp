#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <LittleFS.h>

#include <config.h>
#include <time_manager.h>
#include <record_manager.h>
#include <web_manager.h>
#include <telegram_manager.h>
#include <frame_decoder.h>
#include <ESPmDNS.h>
#include <relay_manager.h>
#include <ap_web_manager.h>
#include <smo_simulator.h>

//==================================================
// SYSTEM OBJECTS
//==================================================
HardwareSerial GDOSerial(1); // UART1 receives frames from the GDO.
HardwareSerial SBUart(0);    // Dedicated UART for simulated SMO-to-SB-001 communication.
Preferences preferences;

//==================================================
// TELEGRAM COMMAND STATE
//==================================================
long lastUpdateId = 0; // Prevents previously handled Telegram commands from being processed again.

//==================================================
// UART STATE MACHINE AND RAW-DATA RING BUFFER
//==================================================
enum UartState
{
  WAIT_AA1,
  WAIT_AA2,
  RECEIVE_DATA
};

UartState uartState = WAIT_AA1;
uint8_t rxBuf[32];
uint8_t rxIndex = 0;

//==================================================
// UART FRAME QUEUE
// readUART() captures frames quickly; decoding, Web, Telegram, and LittleFS
// processing is deferred until the frames are removed from this queue.
//==================================================
uint8_t frameQueue[FRAME_QUEUE_SIZE][FRAME_SIZE];
unsigned long frameQueueTime[FRAME_QUEUE_SIZE];

uint8_t frameHead = 0;
uint8_t frameTail = 0;
uint8_t frameCount = 0;

unsigned long frameStartTime = 0;
unsigned long pendingStartMillis = 0;
bool pendingError = false;
uint8_t pendingFrame[FRAME_SIZE];
unsigned long pendingRxTime = 0;
String pendingTimestamp = "";
String pendingRawFrame = "";
String pendingMsg = "";
bool pendingCritical = false;
volatile uint32_t uartByteCounter = 0;
volatile uint32_t uartFrameCounter = 0;
volatile uint32_t uartFooterDropCounter = 0;
volatile uint32_t uartChecksumDropCounter = 0;
volatile uint32_t uartFilterDropCounter = 0;
unsigned long lastUartByteMillis = 0;
unsigned long lastUartFrameMillis = 0;

//==================================================
// TEN MOST RECENT RAW FRAMES FOR THE /viewraw COMMAND
//==================================================
struct FrameLog
{
  uint8_t data[11];
  unsigned long timestamp;
};

FrameLog rawBuffer[10];
uint8_t rawBufferIndex = 0;
uint8_t totalFramesStored = 0;

//==================================================
// FUNCTION PROTOTYPES
//==================================================

// Telegram command
bool checkTelegramCommands();
void handleTelegramTask();
void beginTelegramCommandTask();
void sendHelpMenu();
void sendStatusReport();
void sendRawLog();
void sendWifiInfo();
// UART receive and queue
void readUART();
bool enqueueFrame(uint8_t *frame, unsigned long rxTime);
bool dequeueFrame(uint8_t *frame, unsigned long *rxTime);
void processQueuedFrames();
void storeFrameToBuffer(uint8_t *buf);

void sendAPInfo();
bool telegramCommandMatches(const String &text, const String &command);

// Frame processing
void processFrame(uint8_t *buf, unsigned long rxTime);
String frameToHex(uint8_t *buf);
bool shouldStoreToWeb(uint8_t errorByte);
void confirmPendingError();
uint32_t getUartByteCounter();
uint32_t getUartFrameCounter();
uint32_t getUartFooterDropCounter();
uint32_t getUartChecksumDropCounter();
uint32_t getUartFilterDropCounter();
unsigned long getLastUartByteAgeMs();
unsigned long getLastUartFrameAgeMs();

//==================================================
// SETUP
//==================================================
void setup()
{
#if ENABLE_SERIAL_LOG
  Serial.begin(115200);
#endif
  delay(1000);

  // Mount LittleFS so Web records can be restored after a reset or power loss.
  if (!LittleFS.begin(true))
  {
    Serial.println("[FS] LittleFS Mount Failed");
  }
  else
  {
    Serial.println("[FS] LittleFS Mounted");

    // Print LittleFS capacity information.
    Serial.println("===== LITTLEFS INFO =====");

    Serial.print("Total: ");
    Serial.print(LittleFS.totalBytes() / 1024);
    Serial.println(" KB");

    Serial.print("Used : ");
    Serial.print(LittleFS.usedBytes() / 1024);
    Serial.println(" KB");

    Serial.print("Free : ");
    Serial.print((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
    Serial.println(" KB");

    loadRecordsFromStorage();
  }

  // Configure I/O and restore settings stored in Preferences.
  preferences.begin("gdo_jig", false);
#if ENABLE_RF_CONTROL
  initRelays();
  loadRelayCounters();
#endif
  lastUpdateId = preferences.getInt("lastUpdateId", 0);

  Serial.print("\n--- ");
  Serial.print(DEVICE_NAME);
  Serial.println(" GDO JIG TESTER START ---");

  // Start the UART that receives frames from the GDO.
  GDOSerial.begin(UART_BAUDRATE, SERIAL_8N1, PIN_SMO_UART_RX, PIN_SMO_UART_TX);

#if ENABLE_SMO_SIMULATOR
  SBUart.begin(UART_BAUDRATE, SERIAL_8N1, PIN_SB_UART_RX, PIN_SB_UART_TX);
  setupSmoSimulator(SBUart);
#endif

  // Start WiFiManager to configure or connect to Wi-Fi.
  WiFiManager wm;
  // wm.resetSettings(); // Uncomment to erase saved Wi-Fi settings and reconfigure them.

  Serial.println("Connecting Wi-Fi via WiFiManager...");
  if (!wm.autoConnect("ESP_Jig_Setup"))
  {
    Serial.println("Failed to connect Wi-Fi and hit timeout. Restarting ESP...");
    ESP.restart();
  }

  Serial.println("Wi-Fi Connected Successfully!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(MDNS_NAME))
  {
    Serial.println("[mDNS] Started");
    Serial.print("[mDNS] URL: http://");
    Serial.print(MDNS_NAME);
    Serial.println(".local");
  }
  else
  {
    Serial.println("[mDNS] Failed");
  }

  // Send a startup notification to Telegram.
  beginTelegramCommandTask();

  String initMsg = String(DEVICE_NAME) + " Online!\n";
  sendTelegramHTML(initMsg);


  // Synchronize NTP time and start the Web servers.
  setupTime();
  setupWebServer();
  setupAPWebServer();

  Serial.print("[WEB] Open browser: http://");
  Serial.println(WiFi.localIP());
}

//==================================================
// LOOP
//==================================================
void loop()
{
#if ENABLE_RF_CONTROL
  updateRelays();
#endif

  // Process Web Dashboard requests.
  handleWebClient();

  readUART();
  processQueuedFrames();
  if (pendingError && millis() - pendingStartMillis >= 100)
  {
    confirmPendingError();
  }

#if ENABLE_SMO_SIMULATOR
  handleSmoSimulator();
#endif
  processRecordStorage();
}


//======================================
// TELEGRAM COMMAND PROCESSING
//======================================
bool checkTelegramCommands()
{
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(1500);
  client.setHandshakeTimeout(4);

  if (!client.connect("api.telegram.org", 443))
    return false;

  String url = "/bot" + String(BOT_TOKEN) + "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=10";
  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: api.telegram.org\r\n" + "Connection: close\r\n\r\n");

  // Skip the HTTP headers and keep only the JSON response body.
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    if (line == "\r")
      break;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, client);
  if (error)
    return false;

  JsonArray results = doc["result"].as<JsonArray>();
  long maxSeenUpdateId = lastUpdateId;

  for (JsonObject result : results)
  {
    long updateId = result["update_id"];
    if (updateId > maxSeenUpdateId)
      maxSeenUpdateId = updateId;

    String chatId = result["message"]["chat"]["id"].as<String>();
    if (chatId != String(CHAT_ID))
      continue;

    String text = result["message"]["text"].as<String>();
    text.trim();

    String suffix = String(TELEGRAM_COMMAND_SUFFIX);
    String cmdStatus = "/status" + suffix;
    String cmdHelp = "/help" + suffix;
    String cmdRaw = "/viewraw" + suffix;
    String cmdClear = "/clear" + suffix;
    String cmdWifi = "/wifi" + suffix;
    String cmdAP = "/ap" + suffix;

    // Report the current status (/status).
    if (telegramCommandMatches(text, cmdStatus))
    {
      sendStatusReport();
    }
    // Return the raw UART ring-buffer log (/viewraw).
    else if (telegramCommandMatches(text, cmdRaw))
    {
      sendRawLog();
    }
    // Clear all Web and LittleFS records (/clear).
    else if (telegramCommandMatches(text, cmdClear))
    {
      clearRecords();

      if (!sendTelegramHTMLNow("Web error log cleared.\nStored Records: 0/50"))
        sendTelegramHTMLPriority("Web error log cleared.\nStored Records: 0/50");
    }
    // Return Wi-Fi information and the Web URL (/wifi).
    else if (telegramCommandMatches(text, cmdWifi))
    {
      sendWifiInfo();
    }
    // Return access-point information and its Web URL (/ap).
    else if (telegramCommandMatches(text, cmdAP))
    {
      sendAPInfo();
    }
    // Return the help menu only when the help command matches this device.
    else if (telegramCommandMatches(text, cmdHelp))
    {
      sendHelpMenu();
    }
    else
    {
      Serial.println("[Telegram] Command ignored for this device");
    }
  }

  if (maxSeenUpdateId > lastUpdateId)
  {
    lastUpdateId = maxSeenUpdateId;
    preferences.putInt("lastUpdateId", lastUpdateId);
  }

  return true;
}

bool telegramCommandMatches(const String &text, const String &command)
{
  if (text.equalsIgnoreCase(command))
    return true;

  String mentionPrefix = command + "@";
  if (text.length() > mentionPrefix.length() &&
      text.substring(0, mentionPrefix.length()).equalsIgnoreCase(mentionPrefix))
  {
    return true;
  }

  return false;
}

static bool telegramCommandTaskStarted = false;

static void telegramCommandTask(void *parameter)
{
    (void)parameter;

    unsigned long lastTelegramCheck = 0;
    unsigned long telegramPauseUntil = 0;

    while (true)
    {
        if (WiFi.status() == WL_CONNECTED && millis() >= telegramPauseUntil)
        {
            if (millis() - lastTelegramCheck >= 500)
            {
                lastTelegramCheck = millis();
                bool ok = checkTelegramCommands();
                if (!ok)
                {
                    Serial.println("[Telegram] Command check failed. Pausing command checks for 3 seconds.");
                    telegramPauseUntil = millis() + 1500;
                }
            }

            processTelegramQueue();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void beginTelegramCommandTask()
{
    if (telegramCommandTaskStarted)
        return;

    telegramCommandTaskStarted = true;
    xTaskCreatePinnedToCore(
        telegramCommandTask,
        "telegram_cmd",
        8192,
        nullptr,
        1,
        nullptr,
        0);
}

void handleTelegramTask()
{
    static unsigned long lastTelegramCheck = 0;
    static unsigned long telegramPauseUntil = 0;

    if (isAnyRelayAutoRunning())
    {
        return;
    }


    // Skip Telegram polling when Wi-Fi is unavailable.
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    // Pause polling after a Telegram API failure.
    if (millis() < telegramPauseUntil)
    {
        return;
    }

    // Poll Telegram at most once every 4.5 seconds.
    if (millis() - lastTelegramCheck < 4500)
    {
        return;
    }

    lastTelegramCheck = millis();

    bool ok = checkTelegramCommands();
    if (!ok)
    {
        Serial.println("[Telegram] API check failed. Pausing Telegram checks for 30 seconds.");
        telegramPauseUntil = millis() + 30000; // Pause for 30 seconds after an API failure.
    }
}

void sendAPInfo()
{
  String msg = "<b>AP Web Information</b>\n";
  msg += "====================\n";
  msg += "Device: <code>" + String(DEVICE_NAME) + "</code>\n";
  msg += "SSID: <code>" + String(getAPSSID()) + "</code>\n";
  msg += "Password: <code>" + String(getAPPassword()) + "</code>\n";
  msg += "URL: <code>" + getAPURL() + "</code>\n";

  if (!sendTelegramHTMLNow(msg))
    sendTelegramHTMLPriority(msg);
}

// Send the current status report to Telegram.
void sendStatusReport()
{
  String msg = "<b>GDO JIG STATUS REPORT</b>\n";
  msg += "=========================\n";
  msg += "WiFi RSSI: ";
  msg += String(WiFi.RSSI());
  msg += " dBm\n";
  msg += "Device: ";
  msg += DEVICE_NAME;
  msg += "\n";
  msg += "Uptime: ";
  msg += String(millis() / 60000);
  msg += " minutes\n\n";

#if ENABLE_RF_CONTROL
  msg += "<b>RF Button Counters</b>\n";
  for (uint8_t i = 0; i < getRelayCountSize(); i++)
  {
    msg += "Button " + String(i + 1);
    msg += " (PIN " + String(getRelayPin(i)) + ")";
    msg += ": Count <b>" + String(getRelayCount(i)) + "</b>";
    msg += ", Cycle " + String(getRelayCycle(i)) + "s";
    msg += ", Limit " + String(getRelayLimit(i)) + "\n";
  }
  msg += "\n";
#else
  msg += "<b>Mode</b>: Error logger only\n\n";
#endif

  msg += "<i>Use /help" + String(TELEGRAM_COMMAND_SUFFIX) + " to view commands.</i>";
  if (!sendTelegramHTMLNow(msg))
    sendTelegramHTMLPriority(msg);
}

// Send the ten most recent raw frames as hexadecimal text.
void sendRawLog()
{
  if (totalFramesStored == 0)
  {
    if (!sendTelegramHTMLNow("Raw UART buffer is empty."))
      sendTelegramPriority("Raw UART buffer is empty.");
    return;
  }

  String logMsg = "<b>Last UART Frames</b>\n";
  logMsg += "Device: <code>" + String(DEVICE_NAME) + "</code>\n";
  logMsg += "====================================\n";

  // Walk the ring buffer backward from the newest frame to the oldest.
  int count = 0;
  int idx = (rawBufferIndex - 1 + 10) % 10;
  int framesToPrint = (totalFramesStored < 10) ? totalFramesStored : 10;

  while (count < framesToPrint)
  {
    char timeStr[16];
    sprintf(timeStr, "[+%02lds]: ", (millis() - rawBuffer[idx].timestamp) / 1000);
    logMsg += "<code>" + String(timeStr);

    for (int i = 0; i < 11; i++)
    {
      char hex[4];
      sprintf(hex, "%02X ", rawBuffer[idx].data[i]);
      logMsg += String(hex);
    }
    logMsg += "</code>\n";

    idx = (idx - 1 + 10) % 10;
    count++;
  }

  if (!sendTelegramHTMLNow(logMsg))
    sendTelegramHTMLPriority(logMsg);
}

// Send the Telegram command help menu.
void sendHelpMenu()
{
  String suffix = String(TELEGRAM_COMMAND_SUFFIX);
  String helpMsg = "<b>ESP32 GDO Monitor</b>\n";
  helpMsg += "Device: <code>" + String(DEVICE_NAME) + "</code>\n";
  helpMsg += "=======================\n\n";
  helpMsg += "<b>Commands</b>\n";
  helpMsg += "<code>/status" + suffix + "</code> : System status.\n";
  helpMsg += "<code>/viewraw" + suffix + "</code> : Last 10 UART frames.\n";
  helpMsg += "<code>/clear" + suffix + "</code> : Clear web error log.\n";
  helpMsg += "<code>/wifi" + suffix + "</code> : WiFi information.\n";
  helpMsg += "<code>/ap" + suffix + "</code> : AP web information.\n";
  helpMsg += "<code>/help" + suffix + "</code> : Show this menu.\n\n";

  if (!sendTelegramHTMLNow(helpMsg))
    sendTelegramHTMLPriority(helpMsg);
}

void sendWifiInfo()
{
  String msg = "<b>WiFi Information</b>\n";
  msg += "====================\n";
  msg += "Device: <code>" + String(DEVICE_NAME) + "</code>\n";
  msg += "SSID: <code>" + WiFi.SSID() + "</code>\n";
  msg += "IP: <code>" + WiFi.localIP().toString() + "</code>\n";
  msg += "Web: <code>http://" + String(MDNS_NAME) + ".local</code>";

  if (!sendTelegramHTMLNow(msg))
    sendTelegramHTMLPriority(msg);
}

//==================================================
// UART FRAME QUEUE
//==================================================
bool enqueueFrame(uint8_t *frame, unsigned long rxTime)
{
  if (frameCount >= FRAME_QUEUE_SIZE)
  {
    Serial.println("[UART] Frame queue full");
    return false;
  }

  memcpy(frameQueue[frameHead], frame, FRAME_SIZE);
  frameQueueTime[frameHead] = rxTime;

  frameHead = (frameHead + 1) % FRAME_QUEUE_SIZE;
  frameCount++;

  return true;
}

bool dequeueFrame(uint8_t *frame, unsigned long *rxTime)
{
  if (frameCount == 0)
  {
    return false;
  }

  memcpy(frame, frameQueue[frameTail], FRAME_SIZE);
  *rxTime = frameQueueTime[frameTail];

  frameTail = (frameTail + 1) % FRAME_QUEUE_SIZE;
  frameCount--;

  return true;
}

void processQueuedFrames()
{
  uint8_t frame[FRAME_SIZE];
  unsigned long rxTime = 0;

  while (dequeueFrame(frame, &rxTime))
  {
    processFrame(frame, rxTime);
  }
}

//======================================
// UART READER AND CIRCULAR RAW-FRAME BUFFER
//======================================
void readUART()
{
  static unsigned long lastByteTime = 0;

  while (GDOSerial.available())
  {
    uint8_t c = GDOSerial.read();
    unsigned long now = millis();
    uartByteCounter++;
    lastUartByteMillis = now;

    // A long inter-byte gap marks the start of a new frame.
    // A long inter-byte gap invalidates a partial frame and restarts synchronization.
    if (now - lastByteTime > UART_FRAME_GAP_MS)
    {
      rxIndex = 0;
      uartState = WAIT_AA1;
    }

    lastByteTime = now;

    switch (uartState)
    {
    case WAIT_AA1:
      if (c == 0xAA)
      {
        frameStartTime = millis(); // Timestamp of the frame's first byte.
        rxBuf[0] = c;
        rxIndex = 1;
        uartState = WAIT_AA2;
      }
      break;

    case WAIT_AA2:
      if (c == 0xAA)
      {
        rxBuf[1] = c;
        rxIndex = 2;
        uartState = RECEIVE_DATA;
      }
      else
      {
        rxIndex = 0;
        uartState = WAIT_AA1;
      }
      break;

    case RECEIVE_DATA:
      rxBuf[rxIndex++] = c;

      if (rxIndex >= FRAME_SIZE)
      {
        if (rxBuf[FRAME_SIZE - 2] == 0x55 &&
            rxBuf[FRAME_SIZE - 1] == 0x55)
        {
          storeFrameToBuffer(rxBuf);
          enqueueFrame(rxBuf, frameStartTime);
          uartFrameCounter++;
          lastUartFrameMillis = millis();
        }
        else
        {
          uartFooterDropCounter++;
          Serial.print("[DROP] Raw = ");
          for (uint8_t i = 0; i < FRAME_SIZE; i++)
          {
            if (rxBuf[i] < 0x10)
              Serial.print("0");

            Serial.print(rxBuf[i], HEX);
            Serial.print(" ");
          }
          Serial.println();
          Serial.println("Invalid Footer Frame dropped");
        }

        rxIndex = 0;
        uartState = WAIT_AA1;
      }
      break;
    }
  }
}

// Store a raw frame together with its receive timestamp.
void storeFrameToBuffer(uint8_t *buf)
{
  memcpy(rawBuffer[rawBufferIndex].data, buf, 11);
  rawBuffer[rawBufferIndex].timestamp = millis();

  rawBufferIndex = (rawBufferIndex + 1) % 10; // Advance and wrap the ring-buffer index.
  if (totalFramesStored < 10)
    totalFramesStored++;
}

// Convert a binary frame to hexadecimal text for Web and Telegram output.
String frameToHex(uint8_t *buf)
{
  String result = "";

  for (int i = 0; i < 11; i++)
  {
    char hex[4];
    sprintf(hex, "%02X ", buf[i]);
    result += hex;
  }

  return result;
}

// Store only errors that require a Web alert.
bool shouldStoreToWeb(uint8_t errorByte)
{
  switch (errorByte)
  {
  case 0x01: // Motor timeout
  case 0x02: // Over force
  case 0x03: // IR Blocked
  case 0x04: // Remote Disabled
  case 0x05: // Wall Console Short
    return true;

  default:
    return false;
  }
}

void confirmPendingError()
{
  if (!pendingError)
    return;

  if (pendingCritical)
  {
    String fullMsg =
        "[" + pendingTimestamp + "]\n" +
        pendingMsg +
        "\nDevice: " + String(DEVICE_NAME) +
        "\nSource: SMO";

    sendTelegram(fullMsg);
  }

  addRecord(pendingTimestamp, pendingRawFrame, pendingMsg);

  Serial.print("[WEB RECORD] Count = ");
  Serial.println(recordCount);

  pendingError = false;
}

uint32_t getUartByteCounter()
{
  return uartByteCounter;
}

uint32_t getUartFrameCounter()
{
  return uartFrameCounter;
}

uint32_t getUartFooterDropCounter()
{
  return uartFooterDropCounter;
}

uint32_t getUartChecksumDropCounter()
{
  return uartChecksumDropCounter;
}

uint32_t getUartFilterDropCounter()
{
  return uartFilterDropCounter;
}

unsigned long getLastUartByteAgeMs()
{
  if (lastUartByteMillis == 0)
    return 0;
  return millis() - lastUartByteMillis;
}

unsigned long getLastUartFrameAgeMs()
{
  if (lastUartFrameMillis == 0)
    return 0;
  return millis() - lastUartFrameMillis;
}

//======================================
// FRAME PROCESSING
//======================================
void processFrame(uint8_t *buf, unsigned long rxTime)
{
  // The protocol checksum is XOR over the address, command, length, and payload bytes.
  // Calculate the XOR checksum over bytes 2 through 7.
  uint8_t cs = 0;
  for (int i = 2; i <= 7; i++)
  {
    cs ^= buf[i];
  }

  if (cs != buf[8])
  {
    uartChecksumDropCounter++;
    Serial.println("Checksum Error");
    return;
  }
  // Accept only frames addressed to Wi-Fi.
  // Accept only Wi-Fi status-report frames with the expected two-byte payload.
  if (buf[3] != 0x02)
  {
    uartFilterDropCounter++;
    return;
  }

  // Accept only status-report commands.
  if (buf[4] != 0x03)
  {
    uartFilterDropCounter++;
    return;
  }

  // Accept only frames with a two-byte payload.
  if (buf[5] != 0x02)
  {
    uartFilterDropCounter++;
    return;
  }

  uint8_t statusByte = buf[6];
  uint8_t errorByte = buf[7];
#if ENABLE_RF_CONTROL
  confirmRelayPressByDoorState(statusByte >> 4);
#endif

  bool isCriticalError = false;
  String msg = decodeMessage(statusByte, errorByte, isCriticalError);
  String timestamp = getTimestamp();
  String rawFrame = frameToHex(buf);

  if (errorByte == 0x00)
  {
    const unsigned long delta = rxTime - pendingRxTime;

    // A near-immediate No Error frame cancels the provisional error as noise.
    if (pendingError &&
        delta < 40)
    {
      Serial.println("[FRAME] Pending error canceled by No Error");
      pendingError = false;
    }

    return;
  }

  bool shouldLogWeb = shouldStoreToWeb(errorByte);
  bool shouldNotifyTelegram = isCriticalError;

  if (!shouldLogWeb && !shouldNotifyTelegram)
  {
    Serial.println("[FRAME] Ignored");
    return;
  }

  // Ignore an identical pending error received within 500 milliseconds.
  // Suppress a repeated copy of the same provisional error within 500 ms.
  if (pendingError &&
      memcmp(buf, pendingFrame, FRAME_SIZE) == 0 &&
      (rxTime - pendingRxTime <= 500))
  {
    Serial.println("[FRAME] Duplicate pending frame skipped");
    return;
  }

  memcpy(pendingFrame, buf, FRAME_SIZE);
  pendingRxTime = rxTime;
  pendingStartMillis = millis();
  Serial.print("[PENDING] Start = ");
  Serial.println(pendingStartMillis);

  pendingTimestamp = timestamp;
  pendingRawFrame = rawFrame;
  pendingMsg = msg;
  pendingCritical = shouldNotifyTelegram;
  pendingError = true;

  Serial.println("[FRAME] Error pending 100ms");
}



