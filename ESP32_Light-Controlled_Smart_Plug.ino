/*
 * ESP32 Lux-Based Tuya Smart Plug Controller
 * 
 * Monitors ambient light using a VEML7700 sensor.
 * During the active window (3pm–9pm), if lux stays below
 * the threshold for 60 seconds consecutively, the Tuya
 * smart plug is turned on automatically.
 * 
 * Hardware: ESP32-WROOM-32D + VEML7700 (I2C)
 * Dependencies: Adafruit VEML7700, Adafruit BusIO, ArduinoJson
 * (all others are included with the ESP32 Arduino core)
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include "time.h"
#include <sys/time.h>
#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include "esp_bt.h"
#include "esp_sleep.h"

// ---------------- WIFI ----------------
const char* ssid     = "WiFI_SSID";
const char* password = "WiFI_PASSWORD";

// ---------------- TUYA ----------------
String client_id     = "MY_CLIENT_ID";
String client_secret = "MY_CLIENT_SECRET_ID";
String device_id     = "MY_DEVICE_ID";

String access_token  = "";
WiFiClientSecure client;

// ---------------- VEML7700 ----------------
Adafruit_VEML7700 veml;

// ---------------- STATE ----------------
bool plugIsOn        = false;
bool tokenFetched    = false;
int luxBelowCount = 0;

// ---------------- LUX THRESHOLD ----------------
#define LUX_THRESHOLD   450.0
#define LUX_CONFIRM_COUNT   6   // 6 readings × 10s = 60s confirmation
#define ACTIVE_HOUR_START  15   // 3 PM (24h clock)
#define ACTIVE_HOUR_END    21   // 9 PM (24h clock)

// Polling intervals
#define POLL_INTERVAL_ACTIVE_MS    10000   // 10s during active window
#define POLL_INTERVAL_IDLE_MS     900000   // 15 min outside window (deep sleep)
#define POLL_INTERVAL_PLUGON_MS   900000   // 15 min when plug is on (delay)

// ---------------- SHA256 ----------------
String sha256(String data) {
  byte hash[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char*)data.c_str(), data.length());
  mbedtls_md_finish(&ctx, hash);
  mbedtls_md_free(&ctx);
  String out = "";
  char buf[3];
  for (int i = 0; i < 32; i++) { sprintf(buf, "%02X", hash[i]); out += buf; }
  return out;
}

// ---------------- HMAC SHA256 ----------------
String hmac_sha256(String key, String data) {
  byte result[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key.c_str(), key.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)data.c_str(), data.length());
  mbedtls_md_hmac_finish(&ctx, result);
  mbedtls_md_free(&ctx);
  String out = "";
  char buf[3];
  for (int i = 0; i < 32; i++) { sprintf(buf, "%02X", result[i]); out += buf; }
  return out;
}

// ---------------- TIME HELPERS ----------------
long long getTimeMs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
}

// Returns current local hour (0–23).
// Toronto = UTC-4 (EDT summer) or UTC-5 (EST winter).
int getCurrentHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return -1;
  return timeinfo.tm_hour;
}

void syncTime() {
  // UTC-4 for Toronto EDT. Change to -5*3600 in winter (EST).
  configTime(-4 * 3600, 0, "time.google.com", "pool.ntp.org");
  Serial.print("Syncing time");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) { Serial.print("."); delay(500); }
  Serial.println("\nTime synced OK");
}

// ---------------- GET TOKEN ----------------
void getToken() {
  HTTPClient https;
  client.setInsecure();

  String url = "https://openapi.tuyaus.com/v1.0/token?grant_type=1";
  long long t = getTimeMs();
  String nonce = String(random(100000, 999999));
  String emptyHash = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  String stringToSign = "GET\n" + emptyHash + "\n\n" + "/v1.0/token?grant_type=1";
  String signStr = client_id + String(t) + nonce + stringToSign;
  String sign = hmac_sha256(client_secret, signStr);

  https.begin(client, url);
  https.addHeader("client_id", client_id);
  https.addHeader("sign", sign);
  https.addHeader("t", String(t));
  https.addHeader("nonce", nonce);
  https.addHeader("sign_method", "HMAC-SHA256");

  int code = https.GET();
  String payload = https.getString();
  Serial.println("\nTOKEN RESPONSE: " + payload);

  DynamicJsonDocument doc(2048);
  deserializeJson(doc, payload);
  access_token = doc["result"]["access_token"].as<String>();
  Serial.println("ACCESS TOKEN: " + access_token);

  https.end();
  tokenFetched = true;
}

// ---------------- SEND COMMAND ----------------
void sendCommand(bool state) {
  HTTPClient https;
  client.setInsecure();

  String path = "/v1.0/iot-03/devices/" + device_id + "/commands";
  String fullUrl = "https://openapi.tuyaus.com" + path;
  long long t = getTimeMs();
  String nonce = String(random(100000, 999999));

  String body =
    "{\"commands\":[{\"code\":\"switch_1\",\"value\":" +
    String(state ? "true" : "false") + "}]}";

  String bodyHash = sha256(body);
  bodyHash.toLowerCase();

  String stringToSign = "POST\n" + bodyHash + "\n\n" + path;
  String signStr = client_id + access_token + String(t) + nonce + stringToSign;
  String sign = hmac_sha256(client_secret, signStr);

  https.begin(client, fullUrl);
  https.addHeader("client_id", client_id);
  https.addHeader("access_token", access_token);
  https.addHeader("t", String(t));
  https.addHeader("nonce", nonce);
  https.addHeader("sign_method", "HMAC-SHA256");
  https.addHeader("sign", sign);
  https.addHeader("Content-Type", "application/json");

  int code = https.POST(body);
  String payload = https.getString();
  Serial.println("\nCOMMAND RESPONSE: " + payload);
  Serial.print("HTTP Code: "); Serial.println(code);

  https.end();
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);
  esp_bt_controller_disable();
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  // Check time before doing anything expensive
  configTime(-4 * 3600, 0, "time.google.com", "pool.ntp.org");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int hour = timeinfo.tm_hour;
    if (hour < ACTIVE_HOUR_START || hour >= ACTIVE_HOUR_END) {
      Serial.println("Outside active window in setup — going back to deep sleep.");
      esp_deep_sleep(POLL_INTERVAL_IDLE_MS * 1000ULL);
    }
  }

  // Only reaches here if inside active window
  delay(2000);
  Wire.begin();
  if (!veml.begin()) {
    Serial.println("VEML7700 not found! Check wiring.");
    while (1);
  }
  Serial.println("VEML7700 ready");

  WiFi.begin(ssid, password);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected");

  syncTime();
  delay(1000);
}

// ---------------- LOOP ----------------
void loop() {
  int hour = getCurrentHour();

  // ---- OUTSIDE active window: deep sleep ----
  if (hour < ACTIVE_HOUR_START || hour >= ACTIVE_HOUR_END) {
    Serial.print("Outside active window (hour=");
    Serial.print(hour);
    Serial.println("). Entering deep sleep.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_deep_sleep(POLL_INTERVAL_IDLE_MS * 1000ULL);
  }

  // ---- INSIDE active window (3pm–9pm) ----

  // Reconnect WiFi after deep sleep wake
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500); Serial.print("."); attempts++;
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi reconnect failed. Retrying soon.");
      delay(10000);
      return;
    }
    // Re-fetch token after reconnect
    tokenFetched = false;
  }

  // Fetch token if we don't have one
  if (!tokenFetched || access_token == "" || access_token == "null") {
    getToken();
    delay(1000);
  }

  // Read lux
  float lux = veml.readLux(VEML_LUX_AUTO);
  Serial.print("Lux: "); Serial.println(lux);

  // Only turn ON if lux stays below threshold for LUX_CONFIRM_COUNT (6) consecutive readings.
  // Once ON, the plug is never turned off by lux — only manually.
  if (!plugIsOn) {
    if (lux < LUX_THRESHOLD) {
      luxBelowCount++;
      Serial.print("Lux below threshold. Confirmation count: ");
      Serial.print(luxBelowCount);
      Serial.print("/");
      Serial.println(LUX_CONFIRM_COUNT);
      if (luxBelowCount >= LUX_CONFIRM_COUNT) {
        Serial.println("Confirmed — turning plug ON.");
        sendCommand(true);
        plugIsOn = true;
        luxBelowCount = 0;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("WiFi off — plug is on, no longer needed.");
      }
    } else {
      // Lux recovered - reset the counter
      if (luxBelowCount > 0) {
        Serial.println("Lux recovered — resetting confirmation counter.");
      }
      luxBelowCount = 0;
      Serial.print("Lux OK ("); Serial.print(lux); Serial.println(") — plug stays off.");
    }
  } else {
    Serial.println("Plug is ON — leaving it on.");
  }

  // Wait before next lux read (15 min if plug is on, 10s if still monitoring)
  unsigned long sleepMs = plugIsOn ? POLL_INTERVAL_PLUGON_MS : POLL_INTERVAL_ACTIVE_MS;
  delay(sleepMs);
}