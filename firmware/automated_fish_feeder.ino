/*
 * Automated Fish Tank Feeder
 * Hardware: ESP-01 / ESP8266 + hobby servo
 * Features: local web dashboard, manual feeding, two persistent daily schedules,
 *           and live status reporting.
 *
 * Before uploading:
 *   1. Set WIFI_SSID and WIFI_PASSWORD.
 *   2. Confirm SERVO_PIN, closed angle, open angle, and timing for your mechanism.
 *   3. Keep the ESP-01 and servo on a stable regulated supply with a common ground.
 */

#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <Servo.h>
#include <time.h>

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

constexpr uint8_t SERVO_PIN = 2;  // GPIO2 on ESP-01; must remain HIGH during boot.
constexpr int SERVO_CLOSED_ANGLE = 10;
constexpr int SERVO_OPEN_ANGLE = 90;
constexpr unsigned long DISPENSE_OPEN_MS = 900;
constexpr unsigned long SETTLE_MS = 350;

// Sri Lanka Standard Time (UTC+05:30). Change if the feeder is used elsewhere.
constexpr long UTC_OFFSET_SECONDS = 5 * 3600 + 30 * 60;
constexpr int DAYLIGHT_OFFSET_SECONDS = 0;

constexpr uint16_t EEPROM_SIZE = 64;
constexpr uint32_t SETTINGS_MAGIC = 0x46454544;  // "FEED"

struct Settings {
  uint32_t magic;
  uint8_t hour1;
  uint8_t minute1;
  uint8_t hour2;
  uint8_t minute2;
};

Settings settings;
ESP8266WebServer server(80);
Servo feederServo;

String lastFeedSource = "Not fed since startup";
time_t lastFeedEpoch = 0;
uint32_t feedCount = 0;
long lastScheduledMinuteKey = -1;

bool validTime(uint8_t hour, uint8_t minute) {
  return hour < 24 && minute < 60;
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, settings);

  if (settings.magic != SETTINGS_MAGIC ||
      !validTime(settings.hour1, settings.minute1) ||
      !validTime(settings.hour2, settings.minute2)) {
    settings = {SETTINGS_MAGIC, 8, 0, 18, 0};
    saveSettings();
  }
}

String twoDigits(int value) {
  return value < 10 ? "0" + String(value) : String(value);
}

String scheduleText(uint8_t hour, uint8_t minute) {
  return twoDigits(hour) + ":" + twoDigits(minute);
}

String currentTimeText() {
  time_t now = time(nullptr);
  if (now < 100000) return "Waiting for internet time";

  struct tm localTime;
  localtime_r(&now, &localTime);
  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
  return String(buffer);
}

String lastFeedText() {
  if (lastFeedEpoch == 0) return lastFeedSource;

  struct tm localTime;
  localtime_r(&lastFeedEpoch, &localTime);
  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
  return String(buffer) + " (" + lastFeedSource + ")";
}

void dispenseFood(const String& source) {
  feederServo.write(SERVO_OPEN_ANGLE);
  delay(DISPENSE_OPEN_MS);
  feederServo.write(SERVO_CLOSED_ANGLE);
  delay(SETTLE_MS);

  lastFeedEpoch = time(nullptr);
  lastFeedSource = source;
  feedCount++;
}

bool parseClockValue(const String& value, uint8_t& hour, uint8_t& minute) {
  if (value.length() != 5 || value.charAt(2) != ':') return false;
  int parsedHour = value.substring(0, 2).toInt();
  int parsedMinute = value.substring(3, 5).toInt();
  if (parsedHour < 0 || parsedHour > 23 || parsedMinute < 0 || parsedMinute > 59) {
    return false;
  }
  hour = static_cast<uint8_t>(parsedHour);
  minute = static_cast<uint8_t>(parsedMinute);
  return true;
}

String pageHtml() {
  String page;
  page.reserve(5000);
  page += F("<!doctype html><html><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<meta http-equiv='refresh' content='20'>");
  page += F("<title>Fish Feeder</title><style>");
  page += F("body{font-family:system-ui;background:#071c26;color:#ecfbff;margin:0;padding:24px}");
  page += F("main{max-width:720px;margin:auto}.card{background:#103241;border:1px solid #24566a;border-radius:16px;padding:20px;margin:16px 0}");
  page += F("h1{color:#6ed8ff}label{display:block;margin:12px 0 5px}input,button{font:inherit;border-radius:9px;padding:11px}");
  page += F("input{background:#071c26;color:white;border:1px solid #397188}button{background:#1ca6d9;color:white;border:0;font-weight:700;cursor:pointer}");
  page += F(".feed{background:#31a36c;width:100%}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.muted{color:#a8cbd8}@media(max-width:540px){.grid{grid-template-columns:1fr}}");
  page += F("</style></head><body><main><h1>Automated Fish Feeder</h1>");

  page += F("<section class='card'><h2>Status</h2>");
  page += "<p><strong>Device time:</strong> " + currentTimeText() + "</p>";
  page += "<p><strong>Last feed:</strong> " + lastFeedText() + "</p>";
  page += "<p><strong>Feeds since startup:</strong> " + String(feedCount) + "</p>";
  page += "<p><strong>Wi-Fi signal:</strong> " + String(WiFi.RSSI()) + " dBm</p></section>";

  page += F("<section class='card'><h2>Manual control</h2><form method='post' action='/feed'>");
  page += F("<button class='feed' type='submit'>Feed now</button></form></section>");

  page += F("<section class='card'><h2>Daily schedule</h2><form method='post' action='/schedule'><div class='grid'>");
  page += "<div><label for='time1'>Feeding 1</label><input id='time1' name='time1' type='time' value='" + scheduleText(settings.hour1, settings.minute1) + "' required></div>";
  page += "<div><label for='time2'>Feeding 2</label><input id='time2' name='time2' type='time' value='" + scheduleText(settings.hour2, settings.minute2) + "' required></div>";
  page += F("</div><p><button type='submit'>Save schedule</button></p></form>");
  page += F("<p class='muted'>The two feeding times are stored in EEPROM and survive a restart.</p></section>");
  page += F("</main></body></html>");
  return page;
}

void handleHome() {
  server.send(200, "text/html", pageHtml());
}

void handleManualFeed() {
  dispenseFood("Manual web command");
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void handleSchedule() {
  uint8_t hour1, minute1, hour2, minute2;
  if (!server.hasArg("time1") || !server.hasArg("time2") ||
      !parseClockValue(server.arg("time1"), hour1, minute1) ||
      !parseClockValue(server.arg("time2"), hour2, minute2)) {
    server.send(400, "text/plain", "Invalid schedule");
    return;
  }

  settings.hour1 = hour1;
  settings.minute1 = minute1;
  settings.hour2 = hour2;
  settings.minute2 = minute2;
  saveSettings();

  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "");
}

void checkSchedule() {
  time_t now = time(nullptr);
  if (now < 100000) return;

  struct tm localTime;
  localtime_r(&now, &localTime);
  long minuteKey = static_cast<long>(now / 60);
  if (minuteKey == lastScheduledMinuteKey) return;

  bool firstTime = localTime.tm_hour == settings.hour1 && localTime.tm_min == settings.minute1;
  bool secondTime = localTime.tm_hour == settings.hour2 && localTime.tm_min == settings.minute2;
  if (firstTime || secondTime) {
    lastScheduledMinuteKey = minuteKey;
    dispenseFood(firstTime ? "Schedule 1" : "Schedule 2");
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Dashboard: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi connection timed out. Restart after checking credentials.");
  }
}

void setup() {
  Serial.begin(115200);
  loadSettings();

  feederServo.attach(SERVO_PIN);
  feederServo.write(SERVO_CLOSED_ANGLE);
  delay(500);

  connectWiFi();
  configTime(UTC_OFFSET_SECONDS, DAYLIGHT_OFFSET_SECONDS,
             "pool.ntp.org", "time.nist.gov");

  server.on("/", HTTP_GET, handleHome);
  server.on("/feed", HTTP_POST, handleManualFeed);
  server.on("/schedule", HTTP_POST, handleSchedule);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void loop() {
  server.handleClient();
  checkSchedule();
  delay(5);
}
