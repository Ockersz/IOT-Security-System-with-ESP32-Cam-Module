#ifndef RECEIVER_SECRETS_H
#define RECEIVER_SECRETS_H

// Copy this file to "secrets.h" in the same folder and fill in your credentials.
// "secrets.h" is listed in .gitignore so secrets are never committed.

// Wi-Fi credentials
static const char WIFI_SSID[] = "YOUR_WIFI_SSID";
static const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// Firebase project Web API key
#define FIREBASE_API_KEY "YOUR_FIREBASE_API_KEY"

// Authorized Firebase Auth email & password for Storage upload
#define FIREBASE_USER_EMAIL "YOUR_FIREBASE_EMAIL"
#define FIREBASE_USER_PASSWORD "YOUR_FIREBASE_PASSWORD"

// Firebase Storage bucket ID (e.g., "your-project-id.appspot.com")
#define FIREBASE_STORAGE_BUCKET "YOUR_STORAGE_BUCKET_ID"

#endif  // RECEIVER_SECRETS_H
