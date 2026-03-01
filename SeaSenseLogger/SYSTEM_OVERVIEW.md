# SeaSense Logger - System Overview

## Current Implementation Status

### ✅ Complete Components

#### Phase 1: Foundation
- **Configuration System** - JSON-based sensor metadata with full lifecycle tracking
- **Hardware Configuration** - Pin definitions, I2C addresses, timing parameters
- **Abstract Interfaces** - SensorInterface and StorageInterface for modularity

#### Phase 2: Core Sensors
- **EZO Sensor Base Class** - I2C ASCII protocol communication
- **EZO-RTD Temperature** - -126°C to +1254°C, ±0.1°C accuracy
- **EZO-EC Conductivity** - 0.07 to 500,000 µS/cm, temperature-compensated
- **EZO-DO Dissolved Oxygen** - Salinity-compensated + atmospheric pressure compensation (Henry's Law, via NMEA2000 barometric pressure)
- **Salinity Calculation** - Practical Salinity Scale (PSS-78 approximation)
- **Quality Assessment** - Automatic quality indicators (GOOD/FAIR/POOR/ERROR)

#### Phase 3: Dual Storage
- **SPIFFS Storage** - Circular buffer for last 100 records (~8 hours @ 5min intervals)
- **SD Card Storage** - Permanent archive (30+ years on 1GB card)
- **StorageManager** - Orchestrates both, graceful degradation
- **Power-Loss Protection** - Safe write patterns, no data corruption
- **CSV Format** - Full sensor metadata traceability in every row

#### Phase 4: Web UI & Calibration
- **WiFi Access Point** - SeaSense-XXXX, always available at 192.168.4.1
- **WiFi Station Mode** - Optional connection to boat WiFi
- **Web Dashboard** - Real-time sensor monitoring (2-second refresh)
- **REST API** - Full sensor, calibration, storage, and config endpoints
- **Calibration Manager** - Guided workflows with automatic stability detection
- **Calibration Types** - Dry, single-point (±2%), two-point (±1%)

#### Phase 5: NMEA2000 Environment Capture
- **NMEA2000Environment** - Caches live data from NMEA2000 bus via snapshot pattern
- Wind (true/apparent speed and angle), water depth, speed through water
- Air temperature, barometric pressure, humidity
- COG, SOG, heading, pitch, roll
- Web dashboard shows live environment values with 3-second polling
- Always built (no compile-time feature flag)

#### Phase 6: API Upload
- **APIUploader** - Bandwidth-conscious upload to SeaSense API
- Configurable interval and batch size
- Upload progress tracked by record count (persisted in SPIFFS metadata, survives reboots)
- Gentle retry with exponential backoff
- Verbose error diagnostics: auth failure, DNS/connection errors, rate limiting, server errors
- Error detail shown in web UI and serial output
- pH and Dissolved Oxygen sensor types included in API payloads
- GPS source auto-detected: NMEA2000 preferred, onboard NEO-6M fallback

#### Phase 7: System Health & Safe Mode
- **SystemHealth** - Boot-loop detection with NVS persistence
- Automatic safe mode after consecutive crash boots
- Watchdog feeding, error counters, diagnostics
- Clear safe mode via web API or serial command

### Pending
- NMEA2000 PGN generation (transmit sensor data to bus)
- BLE configuration interface
- OTA firmware updates

---

## Architecture

### Data Flow

```
EZO Sensors (I2C)
    ↓
Sensor Reading (every 5 seconds)
    ↓
├─→ Temperature Compensation → EC Sensor
├─→ Salinity Calculation
├─→ Quality Assessment
    ↓
Storage Manager
    ├─→ SPIFFS (circular buffer)
    └─→ SD Card (permanent archive)
    ↓
├─→ Web UI (real-time display)
├─→ NMEA2000 PGNs (serial/CAN)
├─→ API Upload (uncompressed JSON)
└─→ Web Dashboard (environment data)
```

### CSV Data Format

Every sensor reading is logged with complete metadata:

```csv
millis,timestamp_utc,sensor_type,sensor_model,sensor_serial,sensor_instance,calibration_date,value,unit,quality
12345678,,Temperature,EZO-RTD,RTD-12345,1,2024-01-15,18.5,°C,good
12345678,,Conductivity,EZO-EC,EC-67890,0,2024-05-10,42500,µS/cm,good
```

**Benefits:**
- Full sensor provenance in every record
- Audit trail for data quality
- Supports sensor replacement (new serial numbers)
- Regulatory compliance ready

### Storage Capacity

| Storage | Capacity | Records | Duration @ 5min |
|---------|----------|---------|-----------------|
| SPIFFS | 100 records | 100 | 8.3 hours |
| SD 1GB | ~30 KB/day | 3.3M | 31.7 years |
| SD 4GB | ~30 KB/day | 13.3M | 126.9 years |

---

## Hardware Setup

### Pin Connections

```
ESP32          Component
GPIO 8     ←→  I2C SDA (EZO sensors)
GPIO 9     ←→  I2C SCL (EZO sensors)
GPIO 23    ←→  SD Card MOSI
GPIO 19    ←→  SD Card MISO
GPIO 18    ←→  SD Card SCK
GPIO 5     ←→  SD Card CS
GPIO 2     ←→  Status LED
```

### I2C Addresses

- **0x66** - EZO-RTD (Temperature)
- **0x64** - EZO-EC (Conductivity)
- **0x61** - EZO-DO (Dissolved Oxygen)
- **0x63** - EZO-pH (pH)

### Power Requirements

- **ESP32**: 5V USB or 3.3V regulated
- **EZO Sensors**: 3.3V-5V (check your modules)
- **SD Card**: 3.3V (use level shifter if needed)

---

## Web Interface

### Access Points

1. **Access Point Mode** (always available)
   - SSID: `SeaSense-XXXX` (last 4 MAC digits)
   - Password: Set in `config/secrets.h`
   - IP: `http://192.168.4.1`

2. **Station Mode** (optional)
   - Configure WiFi in `config/secrets.h`
   - IP shown in serial console
   - Access from any device on boat network

### Pages

- **`/dashboard`** - Real-time sensor readings, pump status, upload countdown
- **`/data`** - Records with UTC timestamps, upload history, storage stats
- **`/calibrate`** - Guided calibration with read-pulse animation feedback
- **`/settings`** - WiFi, API (Live/Test dropdown), sampling, device config

### API Endpoints

#### Sensors
```
GET  /api/sensors              - All sensor readings
GET  /api/sensor/reading?type=temperature
GET  /api/sensor/reading?type=conductivity
```

#### Calibration
```
POST /api/calibrate
{
  "sensor": "conductivity",
  "type": "single",
  "value": 1413
}

GET  /api/calibrate/status
```

#### Storage
```
GET  /api/data/list            - Storage statistics
GET  /api/data/download        - Download CSV
POST /api/data/clear           - Clear all data
```

#### Environment (NMEA2000)
```
GET  /api/environment          - Live NMEA2000 environment data
```

Response (fields omitted when no data available):
```json
{
  "has_any": true,
  "wind": { "speed_true": 12.5, "angle_true": 45.0, "speed_app": 14.2, "angle_app": 32.0 },
  "water": { "depth": 8.5, "stw": 3.2, "temp_ext": 18.3 },
  "atmosphere": { "air_temp": 22.1, "pressure_hpa": 1013.2, "humidity": 65.0 },
  "navigation": { "cog": 185.0, "sog": 5.4, "heading": 183.0 },
  "attitude": { "pitch": -1.2, "roll": 3.5 }
}
```

#### Pump Control
```
GET  /api/pump/status          - Pump status
POST /api/pump/control         - Start/stop pump
GET  /api/pump/config          - Pump configuration
POST /api/pump/config/update   - Update pump config
```

#### System
```
GET  /api/status               - System status
GET  /api/config               - Device configuration
POST /api/config/update        - Update configuration
POST /api/system/restart       - Restart device
POST /api/config/reset         - Factory reset configuration
POST /api/system/clear-safe-mode - Clear safe mode flag
```

---

## Calibration Procedures

### Temperature (EZO-RTD)

Usually factory calibrated - no field calibration needed unless accuracy drifts.

**Single-Point Calibration:**
1. Place probe in known temperature environment
2. Wait for temperature to stabilize
3. Web UI: Calibrate → Temperature → Single Point
4. Enter reference temperature
5. System waits for stable reading
6. Calibration complete

### Conductivity (EZO-EC)

**Recommended: Dry + Single Point (±2% accuracy)**

1. **Dry Calibration** (zero point - required first)
   - Remove probe, ensure completely dry
   - Web UI: Calibrate → Conductivity → Dry
   - Wait for stable reading
   - System calibrates zero point

2. **Single Point** (1413 µS/cm standard)
   - Fill cup with 1413 µS/cm calibration solution
   - Submerge probe completely
   - Web UI: Calibrate → Conductivity → Single Point
   - Enter: 1413
   - Wait for stable reading (system detects automatically)
   - Calibration complete
   - **Result: ±2% accuracy**

**High Accuracy: Two-Point (±1% accuracy)**

1. Dry calibration (as above)
2. **Low Point**
   - Use 84 or 12,880 µS/cm solution
   - Web UI: Calibrate → Conductivity → Two-Point Low
   - Enter solution value
3. **High Point**
   - Use 1413 or 80,000 µS/cm solution
   - Web UI: Calibrate → Conductivity → Two-Point High
   - Enter solution value
   - **Result: ±1% accuracy**

---

## Serial Console Commands

### Current Output

```
===========================================
   SeaSense Logger - Starting Up
===========================================

[CONFIG] Device GUID: seasense-001
[I2C] Initializing I2C bus...
[SENSORS] Initializing sensors...
[SENSORS] EZO-RTD Temperature sensor initialized
[STORAGE] Initializing storage systems...
[STORAGE] SPIFFS initialized successfully
[STORAGE] SD card initialized successfully
[WIFI] Web server started
[WIFI] Access Point: SeaSense-A1B2
[WIFI] AP IP: http://192.168.4.1

===========================================
   SeaSense Logger - Ready
===========================================

--- Sensor Reading ---
Time: 12345678 ms
Temperature: 18.50 °C [GOOD]
Conductivity: 42500 µS/cm [GOOD]
Salinity: 34.52 PSU
----------------------
```

### Future Commands

- `DUMP` - Output CSV data to serial
- `CLEAR` - Delete all data (requires "YES" confirmation)
- `STATUS` - System status and diagnostics
- `TEST` - Read sensors without logging

---

## Configuration Files

### `config/device_config.h`

Complete sensor metadata with lifecycle tracking:

```json
{
  "device_guid": "seasense-001",
  "partner_id": "test-partner",
  "sensors": [
    {
      "type": "Temperature",
      "model": "EZO-RTD",
      "serial_number": "RTD-12345",
      "i2c_address": "0x66",
      "instance": 1,
      "calibration": [
        {
          "date": "2024-01-15T12:00:00Z",
          "type": "factory"
        }
      ]
    }
  ]
}
```

### `config/secrets.h`

WiFi credentials and API keys (git-ignored):

```cpp
// Boat WiFi
#define WIFI_STATION_SSID "BoatWiFi"
#define WIFI_STATION_PASSWORD "your-password"

// SeaSense API
#define API_URL "https://test-api.projectseasense.org"
#define API_KEY "sk_live_your_api_key"
```

---

## Troubleshooting

### Sensors Not Detected

1. Check I2C wiring (SDA, SCL, power, ground)
2. Verify sensor power (3.3V or 5V)
3. Check I2C addresses with scanner
4. Ensure sensors are not in sleep mode

### SD Card Not Mounting

1. Check SD card is formatted FAT32
2. Verify SPI wiring (MOSI, MISO, SCK, CS)
3. Try different SD card (some cards incompatible)
4. Check power supply (SD cards need stable power)

### WiFi Not Working

1. Check WiFi credentials in `secrets.h`
2. Verify ESP32 board supports 2.4GHz WiFi
3. Look for AP SSID in WiFi networks list
4. Serial console shows IP addresses

### Calibration Fails

1. Ensure probe is clean and dry (for dry calibration)
2. Wait longer for reading to stabilize
3. Use fresh calibration solutions
4. Check solution temperature (25°C ideal)
5. Verify probe is fully submerged

---

## Data Retrieval

### Method 1: SD Card

1. Power off ESP32
2. Remove SD card
3. Insert into computer
4. Copy `/data.csv` file
5. Open in spreadsheet software

### Method 2: Web Interface

1. Connect to web UI
2. Navigate to `/data`
3. Click "Download CSV"
4. File downloads to your device

### Method 3: Serial Console (Future)

1. Open serial monitor
2. Type: `DUMP`
3. CSV data outputs to console
4. Copy/paste or save to file

---

## Next Steps

### Ready to Implement

1. **NMEA2000 PGN Transmission**
   - Transmit sensor readings as standard/custom PGNs
   - CAN bus hardware integration

2. **BLE Configuration**
   - Bluetooth Low Energy interface for mobile app setup
   - Sensor configuration without WiFi

3. **OTA Firmware Updates**
   - Over-the-air updates via web UI or cloud API

### Future Enhancements

1. **Gzip payload compression** (ESP32-targz removed due to ESP32-S3 incompatibility; evaluate alternatives)
2. **Data export formats** (JSON, NetCDF)
3. **Remote configuration** via cloud API

---

## Technical Specifications

### Performance

- **Sampling Rate**: 5 seconds (configurable)
- **Sensor Response Time**: 600ms per sensor
- **Storage Write Time**: <50ms
- **Web UI Update Rate**: 2 seconds
- **Calibration Stability Check**: 5 samples @ 500ms

### Reliability

- **Power-Loss Protection**: Yes (safe write patterns)
- **Data Corruption Risk**: Minimal (journaled filesystems)
- **Graceful Degradation**: Continues with available storage
- **Watchdog Protection**: 10ms loop delay prevents resets

### Scalability

- **Additional Sensors**: Easy to add (follow EZOSensor pattern)
- **Storage Expansion**: SD cards up to 32GB supported
- **Network Capacity**: 4 simultaneous WiFi connections
- **API Rate Limits**: Configurable batch size and interval

---

## Project Structure

```
SeaSenseLogger/
├── SeaSenseLogger.ino          # Main entry point
├── README.md                   # Build and usage instructions
├── SYSTEM_OVERVIEW.md          # This document
│
├── config/
│   ├── hardware_config.h       # Pin definitions, I2C addresses
│   ├── device_config.h         # JSON device metadata
│   ├── device_config.h.template
│   ├── secrets.h               # WiFi, API keys (git-ignored)
│   └── secrets.h.template
│
├── src/
│   ├── sensors/
│   │   ├── SensorInterface.h
│   │   ├── EZOSensor.h/.cpp
│   │   ├── EZO_RTD.h/.cpp
│   │   ├── EZO_EC.h/.cpp
│   │   ├── EZO_pH.h/.cpp
│   │   ├── EZO_DO.h/.cpp
│   │   ├── GPSModule.h/.cpp
│   │   ├── NMEA2000GPS.h/.cpp
│   │   └── NMEA2000Environment.h/.cpp
│   │
│   ├── storage/
│   │   ├── StorageInterface.h
│   │   ├── SPIFFSStorage.h/.cpp
│   │   ├── SDStorage.h/.cpp
│   │   └── StorageManager.h/.cpp
│   │
│   ├── api/
│   │   └── APIUploader.h/.cpp  # Upload with verbose error diagnostics
│   │
│   ├── calibration/
│   │   └── CalibrationManager.h/.cpp
│   │
│   ├── config/
│   │   └── ConfigManager.h/.cpp
│   │
│   ├── system/
│   │   └── SystemHealth.h/.cpp
│   │
│   └── webui/
│       └── WebServer.h/.cpp
│
│   ├── pump/
│   │   └── PumpController.h/.cpp  # 3-state pump cycle controller
│   │
│   ├── commands/
│   │   └── SerialCommands.h/.cpp  # Serial command interface
│   │
└── test/
    ├── Makefile                # Native test runner (make test)
    ├── mocks/                  # Arduino mock headers for native tests
    ├── test_config_clamp.cpp
    ├── test_csv_roundtrip.cpp
    ├── test_gps_nan_guard.cpp
    ├── test_metadata_batching.cpp
    ├── test_millis_to_utc.cpp
    ├── test_millis_rollover.cpp
    ├── test_upload_timing.cpp
    ├── test_upload_tracking.cpp
    └── test_system_health.cpp
```

---

## Contributing

This logger is part of [Project SeaSense](https://projectseasense.org/) - a global network of sailors helping scientists monitor ocean health, track climate change, and detect pollution patterns.

**Join the mission: Sail with Purpose, Make Sense of the Sea.**
