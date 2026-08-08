#include "telegram_manager.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>

static unsigned long lastTelegram = 0;
static const uint8_t TELEGRAM_QUEUE_SIZE = 30;
static const unsigned long TELEGRAM_SEND_INTERVAL_MS = 200;

struct TelegramMessage
{
  String text;
  bool html;
  uint8_t attempts;
};

static TelegramMessage telegramQueue[TELEGRAM_QUEUE_SIZE];
static uint8_t telegramHead = 0;
static uint8_t telegramTail = 0;
static uint8_t telegramCount = 0;
static SemaphoreHandle_t telegramMutex = nullptr;
static bool telegramTaskStarted = false;
static String lastTelegramSendStatus = "IDLE";

static void ensureTelegramMutex()
{
  if (telegramMutex == nullptr)
  {
    telegramMutex = xSemaphoreCreateMutex();
  }
}

static void enqueueTelegram(String msg, bool html)
{
  ensureTelegramMutex();
  xSemaphoreTake(telegramMutex, portMAX_DELAY);

  if (telegramCount >= TELEGRAM_QUEUE_SIZE)
  {
    // Normal messages favor new data by dropping the oldest queued item.
    telegramHead = (telegramHead + 1) % TELEGRAM_QUEUE_SIZE;
    telegramCount--;
  }

  telegramQueue[telegramTail].text = msg;
  telegramQueue[telegramTail].html = html;
  telegramQueue[telegramTail].attempts = 0;
  telegramTail = (telegramTail + 1) % TELEGRAM_QUEUE_SIZE;
  telegramCount++;

  xSemaphoreGive(telegramMutex);
}

static void enqueueTelegramPriority(String msg, bool html)
{
  ensureTelegramMutex();
  xSemaphoreTake(telegramMutex, portMAX_DELAY);

  if (telegramCount >= TELEGRAM_QUEUE_SIZE)
  {
    // Make room at the tail, preserving the current head for the priority insert.
    telegramTail = (telegramTail + TELEGRAM_QUEUE_SIZE - 1) % TELEGRAM_QUEUE_SIZE;
    telegramCount--;
  }

  telegramHead = (telegramHead + TELEGRAM_QUEUE_SIZE - 1) % TELEGRAM_QUEUE_SIZE;
  telegramQueue[telegramHead].text = msg;
  telegramQueue[telegramHead].html = html;
  telegramQueue[telegramHead].attempts = 0;
  telegramCount++;

  xSemaphoreGive(telegramMutex);
}

static bool sendTelegramNow(const String &msg, bool html)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    lastTelegramSendStatus = "NO_WIFI";
    return false;
  }

  WiFiClientSecure client;
  // The embedded client does not maintain a CA store; transport is encrypted but
  // the Telegram certificate is not authenticated here.
  client.setInsecure();
  client.setTimeout(1500);
  client.setHandshakeTimeout(4);
  if (!client.connect("api.telegram.org", 443))
  {
    lastTelegramSendStatus = "CONNECT_FAIL";
    return false;
  }

  String url = "/bot" + String(BOT_TOKEN) + "/sendMessage?chat_id=" + String(CHAT_ID);
  if (html)
    url += "&parse_mode=HTML";
  url += "&text=" + urlEncode(msg);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: api.telegram.org\r\n" + "Connection: close\r\n\r\n");
  client.stop();
  lastTelegramSendStatus = "SENT";
  return true;
}

static bool sameTelegramMessage(const TelegramMessage &a, const TelegramMessage &b)
{
  return a.html == b.html && a.text == b.text;
}

static void telegramTask(void *parameter)
{
  (void)parameter;

  while (true)
  {
    processTelegramQueue();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void beginTelegramTask()
{
  ensureTelegramMutex();

  if (telegramTaskStarted)
    return;

  telegramTaskStarted = true;
  xTaskCreatePinnedToCore(
      telegramTask,
      "telegram_task",
      8192,
      nullptr,
      1,
      nullptr,
      0);
}

void sendTelegram(String msg)
{
  enqueueTelegram(msg, false);
}

void sendTelegramHTML(String msg)
{
  enqueueTelegram(msg, true);
}

void sendTelegramPriority(String msg)
{
  enqueueTelegramPriority(msg, false);
}

void sendTelegramHTMLPriority(String msg)
{
  enqueueTelegramPriority(msg, true);
}

bool sendTelegramHTMLNow(String msg)
{
  return sendTelegramNow(msg, true);
}

String getLastTelegramSendStatus()
{
  return lastTelegramSendStatus;
}

void processTelegramQueue()
{
  ensureTelegramMutex();

  // Copy the head under the mutex, then release it during the blocking network call.
  xSemaphoreTake(telegramMutex, portMAX_DELAY);
  bool hasMessage = telegramCount > 0;
  TelegramMessage msg;
  if (hasMessage)
  {
    msg = telegramQueue[telegramHead];
  }
  xSemaphoreGive(telegramMutex);

  if (!hasMessage)
    return;

  if (millis() - lastTelegram < TELEGRAM_SEND_INTERVAL_MS)
    return;

  lastTelegram = millis();

  xSemaphoreTake(telegramMutex, portMAX_DELAY);
  bool headStillSame = telegramCount > 0 && sameTelegramMessage(telegramQueue[telegramHead], msg);
  xSemaphoreGive(telegramMutex);

  bool sent = sendTelegramNow(msg.text, msg.html);

  xSemaphoreTake(telegramMutex, portMAX_DELAY);
  if (telegramCount > 0 && headStillSame && sameTelegramMessage(telegramQueue[telegramHead], msg))
  {
    // Remove successful messages, or failed messages after three total attempts.
    if (sent || telegramQueue[telegramHead].attempts >= 2)
    {
      telegramHead = (telegramHead + 1) % TELEGRAM_QUEUE_SIZE;
      telegramCount--;
    }
    else
    {
      telegramQueue[telegramHead].attempts++;
    }
  }
  xSemaphoreGive(telegramMutex);
}

String urlEncode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (isalnum(c))
    {
      encodedString += c;
    }
    else if (c == ' ')
    {
      encodedString += "%20";
    }
    else
    {
      encodedString += '%';
      code0 = (c >> 4) & 0xf;
      code1 = c & 0xf;
      encodedString += (char)(code0 > 9 ? code0 - 10 + 'A' : code0 + '0');
      encodedString += (char)(code1 > 9 ? code1 - 10 + 'A' : code1 + '0');
    }
  }
  return encodedString;
}
