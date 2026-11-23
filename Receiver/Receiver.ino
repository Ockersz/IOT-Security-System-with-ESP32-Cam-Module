#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Time.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_now.h>
#include "driver/rtc_io.h"
#include "soc/rtc_cntl_reg.h"  // Disable brownout detector
#include "soc/soc.h"

#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>

#include "secrets.h"

// ============================================================================
// Storage & Capture Configuration
// ============================================================================
static const char* LOCAL_PHOTO_PATH = "/photo.jpg";
static const char* PHOTO_MIME_TYPE = "image/jpeg";
static const uint8_t PHOTOS_PER_ALERT = 5;
static const uint8_t MAX_CAPTURE_RETRIES = 3;
static const unsigned long CAPTURE_INTERVAL_MS = 10000UL;  // 10s between burst captures
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000UL;

// ============================================================================
// AI-Thinker ESP32-CAM (OV2640) Pin Definitions
// ============================================================================
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ============================================================================
// NTP Time Configuration (UTC+5:30 Sri Lanka / IST default)
// ============================================================================
static const char* NTP_SERVER = "pool.ntp.org";
static const long GMT_OFFSET_SEC = 19800;  // +5h 30m in seconds
static const int DAYLIGHT_OFFSET_SEC = 0;

// ============================================================================
// ESP-NOW Payload (must match Sender/Sender.ino)
// ============================================================================
typedef struct MotionMessage {
  char deviceName[20];
  bool movementDetected;
} MotionMessage;

static MotionMessage incomingMessage;
static volatile bool motionTriggered = false;
static volatile uint8_t photosCaptured = 0;
static unsigned long lastCaptureMs = 0;

static FirebaseData fbdo;
static FirebaseAuth auth;
static FirebaseConfig configF;

bool verifyPhotoFile(fs::FS& fs) {
  File photoFile = fs.open(LOCAL_PHOTO_PATH, FILE_READ);
  if (!photoFile) {
    return false;
  }
  size_t fileSize = photoFile.size();
  photoFile.close();
  return fileSize > 100;
}

bool capturePhotoToSpiffs() {
  for (uint8_t attempt = 1; attempt <= MAX_CAPTURE_RETRIES; attempt++) {
    Serial.printf("Taking photo (attempt %u/%u)...\n", attempt, MAX_CAPTURE_RETRIES);

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println(F("Camera capture failed"));
      delay(150);
      continue;
    }

    File file = SPIFFS.open(LOCAL_PHOTO_PATH, FILE_WRITE);
    if (!file) {
      Serial.println(F("Failed to open SPIFFS file for writing"));
      esp_camera_fb_return(fb);
      delay(150);
      continue;
    }

    size_t written = file.write(fb->buf, fb->len);
    file.close();
    esp_camera_fb_return(fb);

    Serial.printf("Saved %s (%u bytes)\n", LOCAL_PHOTO_PATH, static_cast<unsigned int>(written));
    if (verifyPhotoFile(SPIFFS)) {
      return true;
    }
  }

  Serial.println(F("Error: Failed to capture valid photo after max retries"));
  return false;
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("Connecting to Wi-Fi"));

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startMs >= WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println(F("\nWi-Fi connection timed out. Restarting..."));
      ESP.restart();
    }
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.print(F("Connected! IP: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("Receiver MAC Address: "));
  Serial.println(WiFi.macAddress());
}

void initSPIFFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println(F("Error mounting SPIFFS. Restarting..."));
    ESP.restart();
  }
  Serial.println(F("SPIFFS mounted successfully"));
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed (0x%x). Restarting...\n", err);
    ESP.restart();
  }
}

void onDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
  (void)mac;
  if (len != static_cast<int>(sizeof(MotionMessage))) {
    Serial.printf("Ignored unexpected ESP-NOW packet size: %d\n", len);
    return;
  }

  memcpy(&incomingMessage, incomingData, sizeof(MotionMessage));
  incomingMessage.deviceName[sizeof(incomingMessage.deviceName) - 1] = '\0';

  Serial.print(F("Alert received from: "));
  Serial.println(incomingMessage.deviceName);
  Serial.print(F("Movement Detected: "));
  Serial.println(incomingMessage.movementDetected ? F("Yes") : F("No"));

  if (incomingMessage.movementDetected) {
    photosCaptured = 0;
    lastCaptureMs = 0;  // Trigger first photo immediately
    motionTriggered = true;
  }
}

void formatTimestamp(char* outBuf, size_t outSize) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    snprintf(outBuf, outSize, "ts_%lu", millis());
    return;
  }
  strftime(outBuf, outSize, "%Y-%m-%d_%H-%M-%S", &timeinfo);
}

void buildRemotePath(char* outBuf, size_t outSize) {
  char safeName[20];
  strncpy(safeName, incomingMessage.deviceName, sizeof(safeName) - 1);
  safeName[sizeof(safeName) - 1] = '\0';
  for (size_t i = 0; safeName[i] != '\0'; i++) {
    if (safeName[i] == ' ' || safeName[i] == '/' || safeName[i] == '\\') {
      safeName[i] = '_';
    }
  }

  char timestamp[24];
  formatTimestamp(timestamp, sizeof(timestamp));
  snprintf(outBuf, outSize, "/data/%s_%s.jpg", safeName, timestamp);
}

void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  initWiFi();
  initSPIFFS();
  initCamera();

  configF.api_key = FIREBASE_API_KEY;
  auth.user.email = FIREBASE_USER_EMAIL;
  auth.user.password = FIREBASE_USER_PASSWORD;
  configF.token_status_callback = tokenStatusCallback;

  Firebase.begin(&configF, &auth);
  Firebase.reconnectWiFi(true);

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("Error initializing ESP-NOW"));
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
  Serial.println(F("ESP32-CAM Receiver ready and listening for ESP-NOW alerts."));
}

void loop() {
  if (!motionTriggered) {
    delay(20);
    return;
  }

  unsigned long now = millis();
  if (lastCaptureMs != 0 && (now - lastCaptureMs < CAPTURE_INTERVAL_MS)) {
    delay(20);
    return;
  }

  lastCaptureMs = now;
  uint8_t shotIndex = photosCaptured + 1;
  Serial.printf("Processing motion capture %u/%u\n", shotIndex, PHOTOS_PER_ALERT);

  if (capturePhotoToSpiffs() && Firebase.ready()) {
    char remotePath[80];
    buildRemotePath(remotePath, sizeof(remotePath));

    Serial.printf("Uploading to Firebase Storage: %s\n", remotePath);
    if (Firebase.Storage.upload(
            &fbdo,
            FIREBASE_STORAGE_BUCKET,
            LOCAL_PHOTO_PATH,
            mem_storage_type_flash,
            remotePath,
            PHOTO_MIME_TYPE)) {
      Serial.printf("Upload succeeded. Download URL: %s\n", fbdo.downloadURL().c_str());
    } else {
      Serial.printf("Upload failed: %s\n", fbdo.errorReason().c_str());
    }
  }

  photosCaptured = shotIndex;
  if (photosCaptured >= PHOTOS_PER_ALERT) {
    motionTriggered = false;
    Serial.println(F("Burst capture complete. Returning to standby."));
  }
}
