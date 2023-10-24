#include <esp_now.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_wifi.h>

// REPLACE WITH YOUR RECEIVER MAC Address
uint8_t broadcastAddress[] = { 0xA8, 0x42, 0xE3, 0x4A, 0x7F, 0x90 };

constexpr char WIFI_SSID[] = "Devil";

// Structure example to send data
// Must match the receiver structure
typedef struct struct_message {
  char deviceName[20];
  bool movementDetected;
} struct_message;

// Create a struct_message called myData
struct_message myData;

esp_now_peer_info_t peerInfo;
bool packetSent = false;  // Flag to track if the packet has been sent

//Initiate led's and buzzer
const int redLedPin = GPIO_NUM_2;
const int greenLedPin = GPIO_NUM_15;
const int buzzerPin = GPIO_NUM_5;

int32_t getWiFiChannel(const char *ssid) {
  if (int32_t n = WiFi.scanNetworks()) {
    for (uint8_t i = 0; i < n; i++) {
      if (!strcmp(ssid, WiFi.SSID(i).c_str())) {
        return WiFi.channel(i);
      }
    }
  }
  return 0;
}

void setup() {
  // Init Serial Monitor
  Serial.begin(115200);
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  pinMode(redLedPin, OUTPUT);
  pinMode(greenLedPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  int32_t channel = getWiFiChannel(WIFI_SSID);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of transmitted packet
  esp_now_register_send_cb([](const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("\r\nLast Packet Send Status:\t");
    if (status == ESP_NOW_SEND_SUCCESS) {
      Serial.println("Delivery Success");
      packetSent = true;  // Set flag to true when packet is successfully sent
      
    } else {
      Serial.println("Delivery Fail");
    }
  });

  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  // Add peer
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("\nFailed to add peer");
    return;
  }

  if (esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 1) == ESP_OK) {
    Serial.println("Movement Detected");

    digitalWrite(redLedPin, 1);   // Turn on the red LED
    digitalWrite(greenLedPin, 0);  // Turn off the green LED
    digitalWrite(buzzerPin, 1);   // Turn on the buzzer
    delay(2000);                   // Wait for 2 seconds
    digitalWrite(redLedPin, 0);    // Turn off the red LED
    digitalWrite(buzzerPin, 0);    // Turn off the buzzer
    digitalWrite(greenLedPin, 1);

    // Set values to send
    strcpy(myData.deviceName, "Area 2");
    myData.movementDetected = true;
  } else {
    digitalWrite(redLedPin, 0);     // Turn off the red LED
    digitalWrite(greenLedPin, 1);  // Turn on the green LED
  }

  // Send message via ESP-NOW until packet is sent successfully
  while (!packetSent) {
    esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));
    delay(100);  // Wait for the transmission to complete
  }
  delay(300000);
  Serial.println("\nGoing to Sleep");
  // Go to deep sleep mode
  esp_deep_sleep_start();
}

void loop() {
  // Empty loop as it won't be executed during deep sleep
}