#ifndef SENDER_SECRETS_H
#define SENDER_SECRETS_H

#include <stdint.h>

// Copy this file to "secrets.h" in the same folder and fill in your values.
// "secrets.h" is listed in .gitignore so credentials are never committed.

// MAC address of the ESP32-CAM Receiver (run WiFi.macAddress() on the Receiver to find it)
static const uint8_t RECEIVER_MAC_ADDRESS[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

// Wi-Fi SSID that the ESP32-CAM Receiver connects to (used to match the ESP-NOW Wi-Fi channel)
static const char WIFI_SSID[] = "YOUR_WIFI_SSID";

// Friendly zone label sent to the Receiver (max 19 characters, used in uploaded filenames)
static const char DEVICE_NAME[] = "Area_1";

#endif  // SENDER_SECRETS_H
