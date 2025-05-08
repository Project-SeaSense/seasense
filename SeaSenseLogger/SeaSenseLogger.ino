// SeaSense logger
//
// https://github.com/Project-SeaSense/seasense
//
// Serial commands that you can type during the first 5s after boot:
//    DUMP: Output all data records from internal SPIFFS
//    CLEAR: Delete all data records from internal SPIFFS
//    UPLOAD: Push all new data records from internal SPIFFS to SeaSense server
//    UPLOAD-ALL: Push all data records from internal SPIFFS to SeaSense server
//    CALIBRATE: Run 30s calibration measurement session
//
// TODO
// - Add a button to trigger calibration
// - Add a button to trigger data upload
// - Add a button to trigger device restart
// - Add support for SD card
// - Add support for GSM card
// - Add support for water_dissolved_oxygen,air_temperature,air_humidity,air_pressure,water_colour,ph,wind_apparent_direction,wind_apparent_speed,heading

#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <time.h>
#include <TimeLib.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_BME280.h>
#include <ArduinoJson.h>

// Forward declarations
time_t getLastUploadTime();
void saveLastUploadTime(time_t timestamp);
void uploadData(bool uploadAll = false);
int getDeviceVersion();
String getDeviceGuid();
int getDeviceVersionFromServer();

// Configure your device and sensors in device_config.h
#include "device_config.h"

// Put your WiFi details and Supabase API key in a secrets.h file ignored by Git
// #define WIFI_SSID "yourssid"
// #define WIFI_PASSWORD "yourpass"
// #define SUPABASE_API_KEY "yourapikey"
#include "secrets.h"

// SeaSense server config
#define SUPABASE_URL "https://arncxalleyggoqzdvgnh.supabase.co"
#define SUPABASE_TABLE "seasense_raw"

// ESP32 pins
#define WATER_TEMP_PIN 4      // DS18B20 data
#define GPS_RX_PIN 16         // GPS TX -> ESP RX
#define GPS_TX_PIN 17         // GPS RX -> ESP TX
#define LED_PIN 2             // Onboard LED
#define I2C_SDA_PIN 21        // I2C SDA
#define I2C_SCL_PIN 22        // I2C SCL
#define I2C_SDA_PIN_2 19      // I2C SDA 2 (BME280)
#define I2C_SCL_PIN_2 18      // I2C SCL 2 (BME280)
#define ADS_I2C_ADDRESS 0x48  // ADS1115 I2C address
#define BME280_ADDRESS 0x76   // BME280 I2C address

// ADS1115 analog input pins
#define ADS_EC_PIN 0         // A0
#define ADS_TURBIDITY_PIN 1  // A1
#define ADS_TDS_PIN 2        // A2

// Calibration values (will be loaded from device.json)
float refVoltageTurbidityClear;
float refVoltageTurbidityCloudy;
float refVoltageEcDemineralisedWater;
float refEcDemineralisedWater;
float refVoltageEc1413;
float refEc1413;
float refVoltageTdsDemineralisedWater;
float refTdsDemineralisedWater;
float refVoltageTds1413;
float refTds1413;

// Data logging interval in ms
const unsigned long LOG_INTERVAL = 5000;

// Timer
const unsigned long serialCommandWindow = 5000;  // After hard boot, 5 seconds to enter serial command
unsigned long startMillis;

// Prepare
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;
Adafruit_ADS1115 ads;
OneWire oneWire(WATER_TEMP_PIN);
DallasTemperature waterTemperatureSensor(&oneWire);
TwoWire I2C_2 = TwoWire(1);
Adafruit_BME280 bme;
bool bmeInitialized = false;

// Do it
void setup() {
  Serial.begin(115200);
  Serial.println("NOTICE: Starting measurement cycle ...");
  Serial.printf("NOTICE: Reset reason: %d\n", esp_reset_reason());

  // Initialise LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialise GPS
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("NOTICE: GPS serial initialized");
  
  // Initialise water temperature sensor
  waterTemperatureSensor.begin();
  Serial.println("NOTICE: Temperature sensor initialized");

  // Initialise SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("ERROR: Could not mount SPIFFS!");
  } else {
    Serial.println("NOTICE: SPIFFS mounted");
    loadCalibrationFromJson();
  }

  // Initialise ADS1115
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!ads.begin(ADS_I2C_ADDRESS, &Wire)) {
    Serial.println("ERROR: ADS1115 not found!");
    while (1) {
      delay(1000);  // Prevent watchdog reset
    }
  }
  else {
    Serial.println("NOTICE: ADS1115 initialized");
    ads.setGain(GAIN_TWOTHIRDS);
  }

  // Initialize second I2C bus and BME280
  I2C_2.begin(I2C_SDA_PIN_2, I2C_SCL_PIN_2);
  delay(100);  // Give sensor time to start
  if (bme.begin(BME280_ADDRESS, &I2C_2)) {
    Serial.println("INFO: BME280 initialized on second I2C bus at 0x76");
    bmeInitialized = true;
  } else {
    Serial.println("ERROR: BME280 not found at 0x76 on second I2C bus.");
  }

  // Start timing right after wake
  unsigned long setupStart = millis();

  // Allow for serial commands after poweron or manual reset
  if (esp_reset_reason() == ESP_RST_POWERON || esp_reset_reason() == ESP_RST_SW) {
    startMillis = millis();
    Serial.println(">>>>> Type DUMP, CLEAR, UPLOAD, UPLOAD-ALL or CALIBRATE within 5 seconds <<<<<");
    while (millis() - startMillis < serialCommandWindow) {
      if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.equalsIgnoreCase("DUMP")) {
          dumpData();
        } else if (cmd.equalsIgnoreCase("CLEAR")) {
          clearData();
        } else if (cmd.equalsIgnoreCase("UPLOAD")) {
          uploadData(false);  // Upload only new data
        } else if (cmd.equalsIgnoreCase("UPLOAD-ALL")) {
          uploadData(true);   // Upload all data
        } else if (cmd.equalsIgnoreCase("CALIBRATE")) {
          calibrateSensors();
        }
      }
      delay(10);  // Prevent watchdog reset
    }
  }

  // Read and log sensors
  readAndLogSensors();

  // Save energy
  unsigned long setupEnd = millis();
  unsigned long cycleDuration = setupEnd - setupStart;
  unsigned long sleepDuration = (LOG_INTERVAL > cycleDuration) ? (LOG_INTERVAL - cycleDuration) : 0;

  Serial.printf("NOTICE: Cycle duration: %lu ms | Sleep for: %lu ms\n", cycleDuration, sleepDuration);

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

// Calculate temperature compensation for voltage readings
float temperatureCompensation(float voltage, float waterTempCelsius) {
  float compensationCoefficient = 1.0 + 0.02 * (waterTempCelsius - 25.0);
  return voltage / compensationCoefficient;
}

// Calculate turbidity percentage based off calibration
float calculateTurbidity(float voltage) {
  float percent = ((voltage - refVoltageTurbidityClear) / (refVoltageTurbidityCloudy - refVoltageTurbidityClear)) * 100.0;
  percent = constrain(percent, 0.0, 100.0);
  return percent;
}

// Calculate TDS based off calibration and temperature compensation
float calculateTds(float voltage, float waterTempCelsius) {
  float compensationVoltage = temperatureCompensation(voltage, waterTempCelsius);
  
  // Linear interpolation between calibration points
  float tdsValue = refEcDemineralisedWater + ((compensationVoltage - refVoltageTdsDemineralisedWater) / 
               (refVoltageTds1413 - refVoltageTdsDemineralisedWater)) * (refTds1413 - refTdsDemineralisedWater);

  if (tdsValue < 0.0) {
    tdsValue = 0.0;
  }

  // Convert from µS/cm to ppm (TDS is typically about half of EC)
  return tdsValue * 0.5;
}

// Calculate EC based off calibration and temperature compensation
float calculateEc(float voltage, float waterTempCelsius) {
  float compensationVoltage = temperatureCompensation(voltage, waterTempCelsius);
  
  // Linear interpolation between calibration points
  float ec = refEcDemineralisedWater + ((compensationVoltage - refVoltageEcDemineralisedWater) / 
           (refVoltageEc1413 - refVoltageEcDemineralisedWater)) * (refEc1413 - refEcDemineralisedWater);
  
  if (ec < 0) ec = 0;

  return ec;
}

void logData(float turbidity, float tds, float ec, float waterTemp, float airTemp, float airHumidity, float airPressure) {
  String filename = "/log.csv";

  bool writeHeader = false;
  if (!SPIFFS.exists(filename)) {
    writeHeader = true;
  } else {
    File test = SPIFFS.open(filename, FILE_READ);
    if (test && test.size() == 0) writeHeader = true;
    test.close();
  }

  // Only log to file if we have a good GPS fix
  bool goodFix = gps.location.isValid() &&
                 gps.hdop.isValid() && gps.hdop.hdop() > 0 && gps.hdop.hdop() < 5.0 &&
                 gps.date.isValid() && gps.time.isValid() &&
                 gps.date.year() > 2020;
  
  if (goodFix) {
    File file = SPIFFS.open(filename, FILE_APPEND);
    if (!file) return;

    char buf[256];
    snprintf(buf, sizeof(buf),
             "%lu,%04d-%02d-%02d %02d:%02d:%02d,%.6f,%.6f,%.1f,%.2f,%.2f,%.2f,%.2f,,%.2f,%.2f,%.2f,,,,,,\n",
             (unsigned long)getGPSEpoch(),
             gps.date.year(),
             gps.date.month(),
             gps.date.day(),
             gps.time.hour(),
             gps.time.minute(),
             gps.time.second(),
             gps.location.lat(),
             gps.location.lng(),
             gps.hdop.hdop(),
             turbidity,
             tds,
             ec,
             waterTemp,
             airTemp,
             airHumidity,
             airPressure
             );

    // Write to serial monitor and file
    Serial.print(buf);
    file.print(buf);
    file.close();
  } else {
    // Just print to serial without logging
    char buf[256];
    snprintf(buf, sizeof(buf),
             "WARN: NOFIX,NOFIX,NOFIX,%.1f,%.2f,%.2f,%.2f,%.2f,,%.2f,%.2f,%.2f,,,,,,, NO DATA LOGGED\n",
             gps.hdop.hdop(),
             turbidity,
             tds,
             ec,
             waterTemp,
             airTemp,
             airHumidity,
             airPressure
             );
    Serial.print(buf);
  }
}

void dumpData() {
  File file = SPIFFS.open("/log.csv");
  if (!file) {
    Serial.println("NOTICE: No data found.");
    return;
  }
  Serial.println("============ BEGIN DATA DUMP ================");
  Serial.println("epoch,timestamp,latitude,longitude,hdop,turbidity (%),TDS (ppm),EC (µS/cm),water temperature (°C)");
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println("============ END DATA DUMP ================");
  file.close();
}

void clearData() {
  SPIFFS.remove("/log.csv");
  SPIFFS.remove("/last_upload.txt");
  Serial.println("NOTICE: All data in log file cleared.");
}

bool connectToWiFi() {
  Serial.println("NOTICE: Connecting to WiFi access point " + String(WIFI_SSID) + " ...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 10) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nERROR: WiFi connection failed.");
    return false;
  }
  Serial.println("\nNOTICE: WiFi connected.");

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  while (time(nullptr) < 100000) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nNOTICE: Time synced.");
  return true;
}

// Get the timestamp of the last uploaded record
time_t getLastUploadTime() {
  if (!SPIFFS.exists("/last_upload.txt")) {
    return 0;  // No previous upload
  }
  
  File file = SPIFFS.open("/last_upload.txt", FILE_READ);
  if (!file) return 0;
  
  String timestamp = file.readStringUntil('\n');
  file.close();
  return timestamp.toInt();
}

// Save the timestamp of the last uploaded record
void saveLastUploadTime(time_t timestamp) {
  File file = SPIFFS.open("/last_upload.txt", FILE_WRITE);
  if (!file) return;
  
  file.println(String(timestamp));
  file.close();
}

void uploadData(bool uploadAll) {
  if (!connectToWiFi()) {
    return;
  }

  // Always check/update device version before upload
  int deviceVersion = getDeviceVersionFromServer();
  if (deviceVersion < 0) {
    Serial.println("ERROR: UPLOAD ABORTED: Device version could not be determined. See error above.");
    WiFi.disconnect(true);
    return;
  }
  String deviceGuid = getDeviceGuid();

  File file = SPIFFS.open("/log.csv", FILE_READ);
  if (!file) {
    Serial.println("ERROR: No log file found.");
    return;
  }

  // Prepare a temporary file for upload
  String tempFilename = "/upload_temp.csv";
  File tempFile = SPIFFS.open(tempFilename, FILE_WRITE);
  if (!tempFile) {
    Serial.println("Failed to create temp upload file.");
    file.close();
    return;
  }

  // Example header (adjust columns as needed)
  tempFile.println("device_guid,device_version,epoch,timestamp,lat,lon,hdop,turbidity,tds,ec,water_temperature,water_dissolved_oxygen,air_temperature,air_humidity,air_pressure,water_colour,ph,wind_apparent_direction,wind_apparent_speed,heading");

  // Get last uploaded epoch
  time_t lastUploadEpoch = getLastUploadTime();
  time_t latestEpoch = lastUploadEpoch;
  bool headerWritten = false;
  int uploadedRows = 0;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    if (line.length() == 0) continue;

    // Always write the header row (starts with non-digit)
    if (!headerWritten) {
      tempFile.println(deviceGuid + "," + String(deviceVersion) + "," + line + ",,,,,,,,,");  // Add empty fields to first data row
      headerWritten = true;
      continue;
    }

    // Parse epoch from the second field (after guid, device_version will be added)
    int firstComma = line.indexOf(',');
    if (firstComma < 0) continue;
    String epochStr = line.substring(0, firstComma);
    time_t epoch = (time_t)epochStr.toInt();

    if (uploadAll || epoch > lastUploadEpoch) {
      // Add empty fields for missing measurements
      String lineWithEmptyFields = line + ",,,,,,,,,";  // 9 empty fields
      tempFile.println(deviceGuid + "," + String(deviceVersion) + "," + lineWithEmptyFields);
      uploadedRows++;
      if (epoch > latestEpoch) latestEpoch = epoch;
    }
  }

  file.close();
  tempFile.close();

  // If no new data, don't upload
  if (uploadedRows == 0) {
    Serial.println("No new data to upload.");
    SPIFFS.remove(tempFilename);
    WiFi.disconnect(true);
    return;
  }

  // Create filename with timestamp
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timestamp[20];
  strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", timeinfo);
  String filename = deviceGuid + "_v" + String(deviceVersion) + "_" + String(timestamp) + ".csv";

  // Prepare for upload
  HTTPClient http;
  String bucket = "seasense-raw-file";
  String url = String(SUPABASE_URL) + "/storage/v1/object/" + bucket + "/" + filename;
  Serial.println("Uploading to Supabase Storage...");
  Serial.println(url);

  File uploadFile = SPIFFS.open(tempFilename, FILE_READ);
  if (!uploadFile) {
    Serial.println("Failed to open temp upload file.");
    WiFi.disconnect(true);
    return;
  }

  http.begin(url);
  http.setTimeout(600000);
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_API_KEY));
  http.addHeader("Content-Type", "text/csv");

  int httpResponseCode = http.sendRequest("POST", &uploadFile, uploadFile.size());

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Upload successful!");
    Serial.println(response);
    String publicURL = String(SUPABASE_URL) + "/storage/v1/object/public/" + bucket + "/" + filename;
    Serial.println("Download URL:");
    Serial.println(publicURL);
    // Save the timestamp of the last uploaded record
    saveLastUploadTime(latestEpoch);
  } else {
    Serial.printf("HTTP error: %d\n", httpResponseCode);
  }

  http.end();
  uploadFile.close();
  SPIFFS.remove(tempFilename);
  WiFi.disconnect(true);
}

// Use GPS to create epoch time
time_t getGPSEpoch() {
  tmElements_t tm;
  tm.Year = gps.date.year() - 1970;
  tm.Month = gps.date.month();
  tm.Day = gps.date.day();
  tm.Hour = gps.time.hour();
  tm.Minute = gps.time.minute();
  tm.Second = gps.time.second();
  return makeTime(tm);
}

// Read and log sensors
void readAndLogSensors() {
  // Process GPS data for 2 seconds
  unsigned long start = millis();
  while (millis() - start < 2000) {
    while (gpsSerial.available()) {
      gps.encode(gpsSerial.read());
    }
    delay(1); // Yield to allow serial buffer to fill
  }

  // Read water temperature
  waterTemperatureSensor.requestTemperatures();
  float waterTemp = waterTemperatureSensor.getTempCByIndex(0);

  // Read turbidity
  float turbidityVoltage = ads.computeVolts(ads.readADC_SingleEnded(ADS_TURBIDITY_PIN));
  float turbidity = calculateTurbidity(turbidityVoltage);
  
  // Read TDS
  float tdsVoltage = ads.computeVolts(ads.readADC_SingleEnded(ADS_TDS_PIN));
  float tds = calculateTds(tdsVoltage, waterTemp);

  // Read EC
  float ecVoltage = ads.computeVolts(ads.readADC_SingleEnded(ADS_EC_PIN));
  float ec = calculateEc(ecVoltage, waterTemp);

  // Read air temperature, humidity and pressure
  float airTemp = NAN, airHumidity = NAN, airPressure = NAN;
  if (bmeInitialized) {
    airTemp = bme.readTemperature();
    airHumidity = bme.readHumidity();
    airPressure = bme.readPressure() / 100.0F; // hPa
    Serial.printf("NOTICE: BME280: Air Temp: %.2f °C, Humidity: %.2f %%, Pressure: %.2f hPa\n", airTemp, airHumidity, airPressure);
  } else {
    Serial.println("WARN: BME280 not initialized, skipping air sensor readings.");
  }

  // Check for bad readings
  if (isnan(turbidity) || isnan(tds) || isnan(ec) || isnan(waterTemp)) {
    Serial.println("Bad reading detected");
    blinkLED(2, 200);  // Blink twice for errors
  } else if (gps.location.isValid()) {
    blinkLED(1, 200);  // Blink once for successful reading with GPS fix
  } else {
    blinkLED(3, 200);  // Blink three times for no GPS fix
  }

  // Print GPS status if no fix
  if (!gps.location.isValid()) {
    Serial.printf("WARN:GPS: No fix --> Satellites: %d | ", gps.satellites.value());
  }

  // Print raw voltages for debugging
  Serial.printf("INFO: Voltages - Turbidity: %.4fV, TDS: %.4fV, EC: %.4fV \n", 
                turbidityVoltage, tdsVoltage, ecVoltage);

  // Log data
  logData(turbidity, tds, ec, waterTemp, airTemp, airHumidity, airPressure);

  Serial.printf("INFO: GPS: location valid=%d, hdop valid=%d (%.2f), date valid=%d (%d), time valid=%d, sats=%d\n",
    gps.location.isValid(),
    gps.hdop.isValid(), gps.hdop.hdop(),
    gps.date.isValid(), gps.date.year(),
    gps.time.isValid(),
    gps.satellites.value());
}

void calibrateSensors() {
  Serial.println("\nStarting 30s calibration measurement session ... make sure to immerse the EC and TDS sensors in either demineralised water or 1413µS/cm water. Take the readings and update the refVoltageTdsDemineralisedWater/refVoltageEcDemineralisedWater or refVoltageTds1413/refVoltageEc1413 variables in the code.");
  unsigned long startTime = millis();
  int readings = 0;
  float sumTdsVoltage = 0;
  float sumEcVoltage = 0;

  while (millis() - startTime < 30000) {  // 30 seconds
    // Read voltages
    float tdsVoltage = ads.computeVolts(ads.readADC_SingleEnded(ADS_TDS_PIN));
    float ecVoltage = ads.computeVolts(ads.readADC_SingleEnded(ADS_EC_PIN));

    // Add to sums
    sumTdsVoltage += tdsVoltage;
    sumEcVoltage += ecVoltage;
    readings++;

    // Print progress
    if (readings % 10 == 0) {  // Every 10 readings
      Serial.printf("Calibration progress: %d%%\n", (millis() - startTime) * 100 / 30000);
    }

    delay(100);  // 10 readings per second
  }

  // Calculate and print averages
  float avgTdsVoltage = sumTdsVoltage / readings;
  float avgEcVoltage = sumEcVoltage / readings;

  Serial.println("\nCalibration measurements complete:");
  Serial.printf("Number of readings: %d\n", readings);
  Serial.printf("Average TDS voltage: %.4f V\n", avgTdsVoltage);
  Serial.printf("Average EC voltage: %.4f V\n", avgEcVoltage);
  Serial.println();
}

int getDeviceVersionFromServer() {
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, deviceJson);
  if (error) {
    Serial.println("ERROR: Failed to parse deviceJson! Aborting upload. Please check and fix your device_config.h.");
    return -1; // abort
  }
  doc.remove("version"); // Remove version if present
  String output;
  serializeJson(doc, output);

  HTTPClient http;
  http.begin("https://arncxalleyggoqzdvgnh.supabase.co/functions/v1/upsert_device_config");
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_API_KEY));
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.POST(output);
  if (httpResponseCode > 0) {
    String response = http.getString();
    StaticJsonDocument<256> respDoc;
    DeserializationError error = deserializeJson(respDoc, response);
    if (!error && respDoc.containsKey("newVersion") && respDoc.containsKey("oldVersion")) {
      int oldVersion = respDoc["oldVersion"];
      int newVersion = respDoc["newVersion"];
      if (newVersion > oldVersion) {
        Serial.print("NOTICE: Device configuration updated. New version uploaded to server: version ");
        Serial.println(newVersion);
      } else {
        Serial.println("NOTICE: Device configuration unchanged. No new version uploaded.");
      }
      // Use newVersion for your data uploads
      return newVersion;
    } else {
      Serial.print("ERROR: Invalid response from Server (upsert_device_config). Aborting upload. Server response: ");
      Serial.println(response);
      Serial.println("ERROR: Please check your config or have the server cleaned up (remove duplicate/corrupt device configs).");
      return -1; // abort
    }
  } else {
    Serial.print("ERROR: HTTP POST failed, code: ");
    Serial.println(httpResponseCode);
    Serial.println("ERROR: Aborting upload. Please check your network or server status.");
    return -1; // abort
  }
  http.end();
}

String getDeviceGuid() {
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, deviceJson);
  if (error) return "";
  String guid = doc["device_guid"].as<String>();
  return guid;
}

void loop() {
  // Empty loop
}

void loadCalibrationFromJson() {
  StaticJsonDocument<4096> doc;
  DeserializationError error = deserializeJson(doc, deviceJson);
  if (error) {
    Serial.println("ERROR: Failed to parse device.json for calibration");
    return;
  }
  JsonArray sensors = doc["sensors"];
  for (JsonObject sensor : sensors) {
    String type = sensor["type"];
    JsonArray cal = sensor["calibration"];
    if (type == "Turbidity") {
      for (JsonObject c : cal) {
        int value = c["value"];
        float voltage = c["measured_voltage"];
        if (value == 0) refVoltageTurbidityClear = voltage;
        if (value == 100) refVoltageTurbidityCloudy = voltage;
      }
    } else if (type == "EC") {
      for (JsonObject c : cal) {
        int value = c["value"];
        float voltage = c["measured_voltage"];
        if (value == 25) {
          refVoltageEcDemineralisedWater = voltage;
          refEcDemineralisedWater = value;
        }
        if (value == 1413) {
          refVoltageEc1413 = voltage;
          refEc1413 = value;
        }
      }
    } else if (type == "TDS") {
      for (JsonObject c : cal) {
        int value = c["value"];
        float voltage = c["measured_voltage"];
        if (value == 25) {
          refVoltageTdsDemineralisedWater = voltage;
          refTdsDemineralisedWater = value;
        }
        if (value == 1413) {
          refVoltageTds1413 = voltage;
          refTds1413 = value;
        }
      }
    }
  }
}