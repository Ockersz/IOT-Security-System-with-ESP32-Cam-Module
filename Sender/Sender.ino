#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <string.h>

#include "secrets.h"

// ============================================================================
// Pin Assignments (ESP32 DevKit — PIR Motion Sensor Node)
// ============================================================================
static const gpio_num_t PIR_SENSOR_PIN = GPIO_NUM_4;  // RTC GPIO supporting EXT0 wakeup
static const int RED_LED_PIN = GPIO_NUM_2;      // Alert indicator LED
static const int GREEN_LED_PIN = GPIO_NUM_15;   // Armed / idle status LED
static const int BUZZER_PIN = GPIO_NUM_5;       // Audible motion alarm buzzer

// ============================================================================
// Timing & Retry Configuration
// ============================================================================
static const uint8_t MAX_SEND_RETRIES = 10;
static const unsigned long RETRY_DELAY_MS = 150UL;
static const unsigned long ALARM_DURATION_MS = 2000UL;
static const unsigned long POST_ALERT_COOLDOWN_MS = 5000UL;

// ============================================================================
// ESP-NOW Payload (must match Receiver/Receiver.ino)
// ============================================================================
typedef struct MotionMessage {
  char deviceName[20];
  bool movementDetected;
} MotionMessage;

static MotionMessage outgoingMessage;
static esp_now_peer_info_t peerInfo;

static volatile bool packetSent = false;
static volatile bool sendFinished = false;

int32_t getWiFiChannel(const char* ssid) {
  int32_t networkCount = WiFi.scanNetworks();
  int32_t matchedChannel = 0;

  if (networkCount > 0) {
    for (int32_t i = 0; i < networkCount; i++) {
      if (strcmp(ssid, WiFi.SSID(i).c_str()) == 0) {
        matchedChannel = WiFi.channel(i);
        break;
      }
    }
  }

  WiFi.scanDelete();
  return matchedChannel;
}

void onDataSent(const uint8_t* macAddr, esp_now_send_status_t status) {
  (void)macAddr;
  Serial.print(F("\r\nLast Packet Send Status:\t"));
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println(F("Delivery Success"));
    packetSent = true;
  } else {
    Serial.println(F("Delivery Fail"));
  }
  sendFinished = true;
}

void triggerLocalAlarm() {
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ALARM_DURATION_MS);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);
}

bool initEspNow() {
  WiFi.mode(WIFI_STA);

  int32_t channel = getWiFiChannel(WIFI_SSID);
  if (channel > 0) {
    esp_wifi_set_channel(static_cast<uint8_t>(channel), WIFI_SECOND_CHAN_NONE);
    Serial.printf("Matched Wi-Fi channel %ld for SSID '%s'\n", static_cast<long>(channel), WIFI_SSID);
  } else {
    Serial.printf("Warning: SSID '%s' not found; using default Wi-Fi channel\n", WIFI_SSID);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("Error initializing ESP-NOW"));
    return false;
  }

  esp_now_register_send_cb(onDataSent);

  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, RECEIVER_MAC_ADDRESS, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println(F("Failed to add ESP-NOW peer"));
    return false;
  }

  return true;
}

void sendMotionAlert() {
  memset(&outgoingMessage, 0, sizeof(outgoingMessage));
  strncpy(outgoingMessage.deviceName, DEVICE_NAME, sizeof(outgoingMessage.deviceName) - 1);
  outgoingMessage.movementDetected = true;

  for (uint8_t attempt = 1; attempt <= MAX_SEND_RETRIES && !packetSent; attempt++) {
    sendFinished = false;
    esp_err_t result = esp_now_send(
        RECEIVER_MAC_ADDRESS,
        reinterpret_cast<const uint8_t*>(&outgoingMessage),
        sizeof(outgoingMessage));

    if (result != ESP_OK) {
      Serial.printf("Attempt %u/%u: esp_now_send error (0x%x)\n", attempt, MAX_SEND_RETRIES, result);
    }

    unsigned long waitStart = millis();
    while (!sendFinished && (millis() - waitStart < RETRY_DELAY_MS)) {
      delay(10);
    }
  }

  if (!packetSent) {
    Serial.println(F("Warning: Receiver did not acknowledge alert before retry limit."));
  }
}

void enterDeepSleep() {
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, LOW);

  esp_sleep_enable_ext0_wakeup(PIR_SENSOR_PIN, 1);
  Serial.println(F("Entering deep sleep until next PIR motion trigger..."));
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PIR_SENSOR_PIN, INPUT);

  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  bool wokeFromMotion = (wakeupReason == ESP_SLEEP_WAKEUP_EXT0);

  if (wokeFromMotion) {
    Serial.println(F("Wakeup cause: PIR motion detected!"));
    triggerLocalAlarm();

    if (initEspNow()) {
      sendMotionAlert();
    }

    delay(POST_ALERT_COOLDOWN_MS);
  } else {
    Serial.println(F("Initial boot: Arming PIR sensor and entering deep sleep."));
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, HIGH);
    delay(1000);
  }

  enterDeepSleep();
}

void loop() {
  // Unused: device sleeps between PIR EXT0 motion wakeups.
}
