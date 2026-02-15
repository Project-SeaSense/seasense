/**
 * SeaSense Logger v2 - Hardware Configuration
 *
 * Pin assignments and I2C addresses for ESP32 hardware
 */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

// ============================================================================
// I2C Configuration - Atlas Scientific EZO Sensors
// ============================================================================

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_FREQUENCY 100000  // 100kHz (standard mode)

// ============================================================================
// EZO Sensor I2C Addresses
// ============================================================================

#define EZO_RTD_ADDR 0x66  // Temperature sensor (EZO-RTD)
#define EZO_EC_ADDR  0x64  // Conductivity sensor (EZO-EC)
#define EZO_DO_ADDR  0x61  // Dissolved oxygen (future)
#define EZO_PH_ADDR  0x63  // pH sensor (future)

// ============================================================================
// SPI Configuration - SD Card
// ============================================================================

#define SD_CS_PIN   5
#define SD_MOSI_PIN 23
#define SD_MISO_PIN 19
#define SD_SCK_PIN  18
#define SD_SPI_FREQUENCY 4000000  // 4MHz

// ============================================================================
// GPS Module Configuration - NEO-6M
// ============================================================================

#define GPS_RX_PIN 16  // ESP32 RX (connects to GPS TX)
#define GPS_TX_PIN 17  // ESP32 TX (connects to GPS RX)
#define GPS_BAUD_RATE 9600  // NEO-6M default baud rate
#define GPS_UPDATE_RATE_HZ 1  // 1Hz update rate

// ============================================================================
// CAN Bus Configuration (future - NMEA2000)
// ============================================================================

#define CAN_TX_PIN 4
#define CAN_RX_PIN 2
#define CAN_SPEED 250000  // 250kbps (NMEA2000 standard)

// ============================================================================
// Status LED
// ============================================================================

#define LED_PIN 2

// ============================================================================
// Pump Control
// ============================================================================

#define PUMP_RELAY_PIN 25  // GPIO 25 for relay control

// Pump timing defaults (milliseconds)
#define PUMP_STARTUP_DELAY_MS 2000        // Pump startup time
#define PUMP_STABILITY_WAIT_MS 3000       // Wait for stable readings
#define PUMP_MEASUREMENT_COUNT 1          // Readings per cycle
#define PUMP_MEASUREMENT_INTERVAL_MS 2000 // Between measurements
#define PUMP_STOP_DELAY_MS 500            // Flush time
#define PUMP_COOLDOWN_MS 55000            // Wait before next cycle (55s)
#define PUMP_CYCLE_INTERVAL_MS 60000      // Total cycle time (1 minute)
#define PUMP_MAX_ON_TIME_MS 30000         // Safety cutoff (30 seconds max)

// LED blink patterns (milliseconds)
#define LED_BLINK_NORMAL 1000      // Normal operation: 1Hz blink
#define LED_BLINK_ERROR 200        // Error: fast blink
#define LED_BLINK_CALIBRATION 100  // Calibration mode: very fast blink
#define LED_BLINK_UPLOAD 500       // API upload in progress: 2Hz blink

// ============================================================================
// Timing Configuration
// ============================================================================

#define SENSOR_SAMPLING_INTERVAL_MS 5000  // 5 seconds between sensor readings
#define EZO_RTD_RESPONSE_TIME_MS 600      // EZO-RTD response time
#define EZO_EC_RESPONSE_TIME_MS 600       // EZO-EC response time
#define NMEA2000_METADATA_INTERVAL_MS 60000  // Send metadata PGNs every 60 seconds

// ============================================================================
// WiFi Configuration
// ============================================================================

// Access Point mode (always enabled)
#define WIFI_AP_SSID_PREFIX "SeaSense-"  // Suffix will be device MAC
#define WIFI_AP_PASSWORD "protectplanet!"  // Default AP password
#define WIFI_AP_CHANNEL 6
#define WIFI_AP_MAX_CONNECTIONS 4
#define WIFI_AP_IP "192.168.4.1"
#define WIFI_AP_GATEWAY "192.168.4.1"
#define WIFI_AP_SUBNET "255.255.255.0"

// Station mode (connect to boat WiFi - credentials in secrets.h)
#define WIFI_STATION_CONNECT_TIMEOUT_MS 10000  // 10 seconds
#define WIFI_STATION_RECONNECT_INTERVAL_MS 60000  // Try to reconnect every 60 seconds

// NTP Configuration (for API time sync)
#define NTP_SERVER "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC 0       // UTC offset (0 for UTC)
#define NTP_DAYLIGHT_OFFSET_SEC 0  // Daylight saving offset

// ============================================================================
// Serial Configuration
// ============================================================================

#define SERIAL_BAUD_RATE 115200
#define SERIAL_COMMAND_BUFFER_SIZE 256

// ============================================================================
// Storage Configuration
// ============================================================================

#define SPIFFS_MOUNT_POINT "/spiffs"
#define SPIFFS_CIRCULAR_BUFFER_SIZE 100  // Keep last 100 records in SPIFFS
#define SD_MOUNT_POINT "/sd"
#define SD_CSV_FILENAME "/sd/seasense_data.csv"
#define SD_WRITE_BUFFER_SIZE 512

// ============================================================================
// NMEA2000 Device Identification
// ============================================================================

// Fixed NMEA2000 device identification (required by API)
#define NMEA2000_MANUFACTURER_CODE 2040  // Example manufacturer code
#define NMEA2000_DEVICE_FUNCTION 145     // Environmental sensor
#define NMEA2000_DEVICE_CLASS 60         // Sensor/Communication Interface
#define NMEA2000_INDUSTRY_GROUP 4        // Marine industry

// ============================================================================
// Debug Configuration
// ============================================================================

// #define DEBUG_SENSORS       // Uncomment to enable sensor debug output
// #define DEBUG_STORAGE       // Uncomment to enable storage debug output
// #define DEBUG_NMEA2000      // Uncomment to enable NMEA2000 debug output
// #define DEBUG_API_UPLOAD    // Uncomment to enable API upload debug output
// #define DEBUG_WIFI          // Uncomment to enable WiFi debug output

#ifdef DEBUG_SENSORS
  #define DEBUG_SENSOR_PRINT(x) Serial.print(x)
  #define DEBUG_SENSOR_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_SENSOR_PRINT(x)
  #define DEBUG_SENSOR_PRINTLN(x)
#endif

#ifdef DEBUG_STORAGE
  #define DEBUG_STORAGE_PRINT(x) Serial.print(x)
  #define DEBUG_STORAGE_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_STORAGE_PRINT(x)
  #define DEBUG_STORAGE_PRINTLN(x)
#endif

#ifdef DEBUG_NMEA2000
  #define DEBUG_NMEA2000_PRINT(x) Serial.print(x)
  #define DEBUG_NMEA2000_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_NMEA2000_PRINT(x)
  #define DEBUG_NMEA2000_PRINTLN(x)
#endif

#ifdef DEBUG_API_UPLOAD
  #define DEBUG_API_PRINT(x) Serial.print(x)
  #define DEBUG_API_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_API_PRINT(x)
  #define DEBUG_API_PRINTLN(x)
#endif

#ifdef DEBUG_WIFI
  #define DEBUG_WIFI_PRINT(x) Serial.print(x)
  #define DEBUG_WIFI_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_WIFI_PRINT(x)
  #define DEBUG_WIFI_PRINTLN(x)
#endif

#endif // HARDWARE_CONFIG_H
