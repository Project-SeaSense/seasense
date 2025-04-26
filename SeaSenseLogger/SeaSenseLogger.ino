// SeaSense logger
//
// Tested with:
// - ESP32 WROOM
// - Arduino TDS sensor (AliExpress)
// - Turbidity sensor (AliExpress)
// - DS18B20 temperature sensor (AliExpress)
// - NEO-8M GPS
//
// TODO
// - add correct calibration values
// - log battery level

#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <time.h>
#include <TimeLib.h>
#include <HTTPClient.h>

// Put your WiFi details and Supabase API key in a secrets.h file ignored by git
// #define WIFI_SSID "yourssid"
// #define WIFI_PASSWORD "yourpass"
// #define SUPABASE_API_KEY "yourapikey"
#include "secrets.h"

// Use test mode in case GPS is not enabled. Expect 25 april 2024 17:05:00, Amsterdam lat/lon.
#define TEST_MODE false

// SeaSense server config
#define SEASENSE_DEVICE_GUID "f00c1844-42db-4309-847b-8fbe0b46bec1"    // Use a guid per device
#define SUPABASE_URL "https://arncxalleyggoqzdvgnh.supabase.co"
#define SUPABASE_TABLE "seasense-raw"

// ESP32 pins
#define TURBIDITY_PIN 36  // GPIO VP
#define EC_PIN 39         // GPIO VN
#define WATER_TEMP_PIN 4  // DS18B20 data
#define GPS_RX_PIN 16     // GPS TX -> ESP RX
#define GPS_TX_PIN 17     // GPS RX -> ESP TX
#define LED_PIN 2         // Onboard LED

// Calibration values
const float refTurbidityClear = 1.12;  // 0% turbidity
const float refTurbidityCloudy = 2.11; // 100% turbidity
const float refEcVoltage1 = 0.1;      // voltage measured in first solution (Volt)
const float refEcValue1 = 84.0;        // known EC in first solution (µS/cm)
const float refEcVoltage2 = 2.30;      // voltage measured in second solution (Volt)
const float refEcValue2 = 1413.0;      // known EC in second solution (µS/cm)

// Data logging interval in ms
const unsigned long LOG_INTERVAL = 5000;

// Timer
const unsigned long serialCommandWindow = 5000;   // After hard boot, 5 seconds to enter serial command
unsigned long startMillis;

// Prepare
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
OneWire oneWire(WATER_TEMP_PIN);
DallasTemperature waterTemperatureSensor(&oneWire);

// Do it
void setup() {
  Serial.begin(115200);
  Serial.println("Woke up, starting measurement cycle ...");

  // Turn off led
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialise
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  waterTemperatureSensor.begin();
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed!");
  }
  unsigned long setupStart = millis();  // start timing right after wake

  // Allow for serial commands after poweron
  if (esp_reset_reason() == ESP_RST_POWERON) {
    startMillis = millis();
    Serial.println("Type DUMP, CLEAR or UPLOAD within 5 seconds:");
    while (millis() - startMillis < serialCommandWindow) {
      if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.equalsIgnoreCase("DUMP")) {
          dumpData();
        } else if (cmd.equalsIgnoreCase("CLEAR")) {
          clearData();
        } else if (cmd.equalsIgnoreCase("UPLOAD")) {
          uploadData();
        }
      }
    }
  }

  // Wait for GPS fix, otherwise we can't log datetime nor location
  if (!TEST_MODE && !gps.location.isValid()) {
    waitForGPSFix();
  } else if (TEST_MODE) {
    Serial.println("TEST_MODE enabled, skipping GPS fix, using 25 april 2024 17:05:00, Amsterdam.");
  }

  // Read turbidity
  float turbidityPercent = calculateTurbidity(analogRead(TURBIDITY_PIN));

  // Read water temperature
  waterTemperatureSensor.requestTemperatures();
  float waterTemp = waterTemperatureSensor.getTempCByIndex(0);

  // Read EC 
  float calibratedEC = calculateCalibratedEC(analogRead(EC_PIN), waterTemp);
  
  // Check for bad readings
  if (isnan(turbidityPercent) || isnan(calibratedEC) || isnan(waterTemp)) {
    Serial.println("Bad reading detected");
    // Bad reading blink
    blinkLED(2, 200);
  } else {
    // Good reading blink
    blinkLED(1, 200);
  }

  // And then log data
  logData(turbidityPercent, calibratedEC, waterTemp);

  // Save energy
  unsigned long setupEnd = millis();
  unsigned long cycleDuration = setupEnd - setupStart;
  unsigned long sleepDuration = (LOG_INTERVAL > cycleDuration) ? (LOG_INTERVAL - cycleDuration) : 0;

  Serial.printf("Cycle duration: %lu ms | Sleep for: %lu ms\n", cycleDuration, sleepDuration);

  esp_sleep_enable_timer_wakeup(sleepDuration * 1000);  // in microseconds
  esp_deep_sleep_start();
}

void blinkLED(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(duration);
    digitalWrite(LED_PIN, LOW);
    delay(duration);
  }
}

// Calculate turbidity percentage based off calibration
float calculateTurbidity(float analogValue) {
  float percent = ((calculateVoltage(analogValue) - refTurbidityClear) / (refTurbidityCloudy - refTurbidityClear)) * 100.0;

  // Serial.printf("Turbidity: ADC=%.0f | V=%.2f V | %.1f%%\n", analogValue, calculateVoltage(analogValue), percent);

  percent = constrain(percent, 0.0, 100.0);

  return percent;
}

// Calculate EC based off calibration and temperature compensation
float calculateCalibratedEC(float analogValue, float temp) {
  float voltage = calculateVoltage(analogValue);

  // Linear interpolation between two reference fluids
  float calibratedEC = (voltage - refEcVoltage1) * (refEcValue2 - refEcValue1) / (refEcVoltage2 - refEcVoltage1) + refEcValue1;

  // 2% per 1°C difference from 25°C
  float compensatedEC = calibratedEC / (1.0 + 0.02 * (temp - 25.0));

  // Serial.printf("voltage: %.2f V | calibratedEc: %.2f µS/cm | compensatedEc: %.2f \n", voltage, calibratedEC, compensatedEC);

  return compensatedEC; // in µS/cm
}

void logData(float turbidity, float calibratedEC, float waterTemp) {
  String filename = "/log.csv";

  bool writeHeader = false;
  if (!SPIFFS.exists(filename)) {
    writeHeader = true;
  } else {
    File test = SPIFFS.open(filename, FILE_READ);
    if (test && test.size() == 0) writeHeader = true;
    test.close();
  }

  File file = SPIFFS.open(filename, FILE_APPEND);
  if (!file) return;

  // Log
  char buf[160];
  snprintf(buf, sizeof(buf),
    "%lu,%04d-%02d-%02d %02d:%02d:%02d,%.6f,%.6f,%.1f,%.2f,%.2f,%.2f\n",
    (unsigned long)getGPSEpoch(),
    TEST_MODE ? 2025 : gps.date.year(),
    TEST_MODE ? 4    : gps.date.month(),
    TEST_MODE ? 25   : gps.date.day(),
    TEST_MODE ? 17   : gps.time.hour(),
    TEST_MODE ? 5    : gps.time.minute(),
    TEST_MODE ? 30   : gps.time.second(),
    TEST_MODE ? 52.379189 : gps.location.lat(),
    TEST_MODE ? 4.899431  : gps.location.lng(),
    TEST_MODE ? 1.0       : gps.hdop.hdop(),
    turbidity,
    calibratedEC,
    waterTemp
  );

  // Write to serial monitor and file
  Serial.print(buf);
  file.print(buf);
  file.close();
}

void dumpData() {
  File file = SPIFFS.open("/log.csv");
  if (!file) {
    Serial.println("No data found.");
    return;
  }
  Serial.println("============ BEGIN DATA DUMP ================");
  Serial.println("epoch,timestamp,latitude,longitude,hdop,turbidity (%),calibrated EC (µS/cm),water temperature (°C)");
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println("============ END DATA DUMP ================");
  file.close();
}

void clearData() {
  SPIFFS.remove("/log.csv");
  SPIFFS.remove("/last_upload.txt");
  Serial.println("All data in log file cleared.");
}

void uploadData() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 10) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed.");
    return;
  }
  Serial.println("\nWiFi connected.");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("Time synced.");

  // Load last uploaded epoch
  unsigned long lastEpoch = 0;
  if (SPIFFS.exists("/last_upload.txt")) {
    File f = SPIFFS.open("/last_upload.txt", FILE_READ);
    if (f) {
      lastEpoch = f.readStringUntil('\n').toInt();
      f.close();
    }
  }

  File file = SPIFFS.open("/log.csv", FILE_READ);
  if (!file) {
    Serial.println("No log data to upload.");
    return;
  }

  unsigned long newestEpoch = lastEpoch;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() < 10 || line.startsWith("epoch")) continue;

    int firstComma = line.indexOf(',');
    unsigned long currentEpoch = line.substring(0, firstComma).toInt();
    if (currentEpoch <= lastEpoch) continue;

    // Parse values from CSV
    char timestamp[24];
    float lat, lon, hdop, turbidity, ec, waterTemp;
    sscanf(line.c_str(), "%*lu,%23[^,],%f,%f,%f,%f,%f,%f",
      timestamp, &lat, &lon, &hdop, &turbidity, &ec, &waterTemp);

    // Upload to Supabase
    if (uploadToSupabase(currentEpoch, timestamp, lat, lon, hdop, turbidity, ec, waterTemp)) {
      newestEpoch = currentEpoch;
      delay(100);
    } else {
      Serial.println("Upload failed, skipping...");
    }
  }

  file.close();

  if (newestEpoch > lastEpoch) {
    File f = SPIFFS.open("/last_upload.txt", FILE_WRITE);
    if (f) {
      f.println(newestEpoch);
      f.close();
      Serial.println("Updated last_upload.txt to: " + String(newestEpoch));
    }
  } else {
    Serial.println("No new data uploaded.");
  }

  WiFi.disconnect(true);
}

bool uploadToSupabase(unsigned long epoch, const char* timestamp, float lat, float lon, float hdop, float turbidity, float ec, float watertemp) {
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/" + SUPABASE_TABLE;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_API_KEY));
  http.addHeader("Prefer", "return=minimal");

  String json = "{";
  json += "\"epoch\":" + String(epoch) + ",";
  json += "\"device\":\"" + String(SEASENSE_DEVICE_GUID) + "\",";
  json += "\"timestamp\":\"" + String(timestamp) + "\",";
  json += "\"lat\":" + String(lat, 6) + ",";
  json += "\"lon\":" + String(lon, 6) + ",";
  json += "\"hdop\":" + String(hdop, 1) + ",";
  json += "\"turbidity\":" + String(turbidity, 2) + ",";
  json += "\"ec\":" + String(ec, 2) + ",";
  json += "\"watertemp\":" + String(watertemp, 2);
  json += "}";

  int httpResponseCode = http.POST(json);
  http.end();

  if (httpResponseCode == 201) {
    Serial.println("Uploaded to Supabase: " + json);
    return true;
  } else {
    Serial.printf("HTTP %d error uploading: %s\n", httpResponseCode, json.c_str());
    return false;
  }
}

void waitForGPSFix() {
  // If there is already a valid fix, exit immediately
  delay(800);  // Let GPS data start flowing
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  if (gps.location.isUpdated() && gps.date.isValid() && gps.time.isValid()) {
    Serial.println("GPS fix already available.");
    return;
  }

  unsigned long lastStatus = 0;

  // Keep waiting until location, date, and time are all valid
  while (!gps.location.isUpdated() || !gps.date.isValid() || !gps.time.isValid()) {
    while (gpsSerial.available()) {
      gps.encode(gpsSerial.read());
    }

    // Print GPS status once per second
    if (millis() - lastStatus > 1000) {
      Serial.printf("Satellites: %d | HDOP: %.1f | Fix: %s\n",
        gps.satellites.value(), gps.hdop.hdop(), gps.location.isValid() ? "YES" : "NO");
      lastStatus = millis();
    }

    delay(800);        // short pause before next check
  }

  Serial.println("GPS fix acquired.");
}

// Use GPS to create epoch time
time_t getGPSEpoch() {
  if (TEST_MODE) return 1714064700;  // Fixed value: 25 april 2024 17:05:00
  tmElements_t tm;
  tm.Year = gps.date.year() - 1970;
  tm.Month = gps.date.month();
  tm.Day = gps.date.day();
  tm.Hour = gps.time.hour();
  tm.Minute = gps.time.minute();
  tm.Second = gps.time.second();
  return makeTime(tm);
}

// Convert ADC reading to voltage (ESP32: 12-bit ADC, 3.3V range)
float calculateVoltage(float analogValue) {
  return analogValue * (3.3 / 4095.0);
}

void loop() {
  // not used
}