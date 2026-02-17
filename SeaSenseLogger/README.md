# SeaSense Logger

ESP32-based water quality logger for Atlas Scientific EZO sensors. Logs temperature and conductivity with full sensor metadata traceability.

## Hardware Requirements

- **ESP32 DevKit** (or compatible)
- **Atlas Scientific EZO-RTD** - Temperature sensor (I2C address 0x66)
- **Atlas Scientific EZO-EC** - Conductivity sensor (I2C address 0x64)
- **NEO-6M GPS module** - For self-reliant time and location (UART2)
- **SD card module** - For permanent data storage
- **Power supply** - USB or external 5V

## Software Requirements

### Arduino IDE Setup

1. **Install Arduino IDE** (v2.0 or later recommended)

2. **Install ESP32 Board Support**
   - Open Arduino IDE
   - Go to File → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Go to Tools → Board → Boards Manager
   - Search for "esp32" and install "esp32 by Espressif Systems"

3. **Install Required Libraries**
   - Open Sketch → Include Library → Manage Libraries
   - Install the following:
     - **ArduinoJson** (v7.x) by Benoit Blanchon
     - **TinyGPSPlus** by Mikal Hart
     - **ESP32-targz** (v1.3.1+) by tobozo — for gzip payload compression

4. **Select Board**
   - Tools → Board → ESP32 Arduino → ESP32 Dev Module
   - Tools → Port → [Select your ESP32's COM port]

## Configuration

### 1. Device Configuration

Edit `config/device_config.h` (or copy from `device_config.h.template`):
- Set your `device_guid` and `partner_id`
- Update sensor serial numbers and calibration dates
- Configure sensor deployment depths

### 2. WiFi and API Keys

Copy `config/secrets.h.template` to `config/secrets.h` and configure:

```cpp
#define WIFI_STATION_SSID "YourBoatWiFi"
#define WIFI_STATION_PASSWORD "your-password"
#define API_KEY "sk_live_your_api_key_here"
```

**⚠️ IMPORTANT:** Never commit `secrets.h` to version control!

## Building and Uploading

1. Open `SeaSenseLogger.ino` in Arduino IDE
2. Verify/Compile: Click the ✓ checkmark icon
3. Upload: Click the → arrow icon
4. Open Serial Monitor (115200 baud) to see output

### Arduino CLI (recommended for reproducible builds)

Use one of these targets:

- **ESP32-S3 (4MB) with larger app partition**
  ```bash
  arduino-cli compile \
    --fqbn "esp32:esp32:esp32s3:PartitionScheme=huge_app,FlashSize=4M" \
    SeaSenseLogger
  ```

- **ESP32-S3 Octal / 16MB layout**
  ```bash
  arduino-cli compile \
    --fqbn "esp32:esp32:esp32s3-octal" \
    SeaSenseLogger
  ```

Or use the helper script:

```bash
cd SeaSenseLogger
./scripts/build.sh s3
./scripts/build.sh s3-octal
```

### Enabling NMEA2000

By default NMEA2000 is disabled (`FEATURE_NMEA2000=0`) so builds work without extra libraries.

To enable it:

1. Install required libraries:
   - `NMEA2000`
   - `N2kMessages`
2. Set compile-time flag:
   - either in `config/hardware_config.h`: `#define FEATURE_NMEA2000 1`
   - or via CLI/script: `ENABLE_N2K=1 ./scripts/build.sh s3-octal`

## Current Status

### Implemented
- Configuration system with JSON metadata
- Abstract sensor and storage interfaces
- EZO sensor base class with I2C ASCII protocol
- EZO-RTD temperature sensor
- EZO-EC conductivity sensor with temperature compensation
- Salinity calculation (simplified PSS-78)
- Quality assessment framework
- Dual storage (SPIFFS + SD card) with power-loss protection
- GPS module integration (NEO-6M) for self-reliant time and location
- Web UI for configuration, calibration, and live NMEA2000 environment dashboard
- API upload with bandwidth management and gzip payload compression
- NMEA2000 environment data capture (wind, depth, heading, attitude, atmosphere)
- pH and Dissolved Oxygen sensor type support in API payloads
- Upload progress tracking (records since last upload)
- Serial command interface (DUMP, CLEAR, STATUS, TEST)
- Safe mode boot-loop detection and system health monitoring

## Features

### Sensor Capabilities
- **Temperature**: -126°C to +1254°C, ±0.1°C accuracy
- **Conductivity**: 0.07 to 500,000 µS/cm, ±1-2% accuracy
- **Salinity**: Calculated from conductivity and temperature
- **Quality tracking**: Each reading assessed for validity and quality

### Data Management
- Full sensor provenance in every data record
- GPS location and time for every reading
- Self-reliant operation (works without WiFi/internet)
- Calibration history tracking
- Dual storage (SPIFFS buffer + SD card permanent)
- CSV format with complete metadata (lat/lon, GPS quality)
- Bandwidth-conscious cloud upload

### Web Interface
- Real-time sensor monitoring dashboard
- Live NMEA2000 environment data (wind, depth, heading, atmosphere, attitude)
- Guided calibration workflow
- Sensor metadata editing
- Data viewing and download
- Upload and system configuration

## Serial Output

At startup, you'll see:
```
===========================================
   SeaSense Logger - Starting Up
===========================================

[CONFIG] Loading device configuration...
[CONFIG] Device GUID: seasense-001
[I2C] Initializing I2C bus...
[SENSORS] Initializing sensors...
[SENSORS] EZO-RTD Temperature sensor initialized
[GPS] GPS module initialized
[GPS] Waiting for GPS fix...

===========================================
   SeaSense Logger - Ready
===========================================

--- Sensor Reading ---
Time: 12345678 ms
GPS: 52.374500° N, 4.889500° E (8 sats, HDOP: 1.2)
GPS Time: 2024-05-15T10:28:23Z
Temperature: 18.50 °C [GOOD]
Conductivity: 42500 µS/cm [GOOD]
Salinity: 34.52 PSU
```

## Pin Assignments

See `config/hardware_config.h` for complete pin configuration:

- **I2C**: SDA=21, SCL=22
- **GPS**: RX=16, TX=17 (UART2)
- **SD Card**: CS=5, MOSI=23, MISO=19, SCK=18
- **Status LED**: GPIO 2
- **CAN Bus** (future): TX=4, RX=2

## Calibration

### Temperature (EZO-RTD)
Usually factory calibrated - no field calibration needed.

### Conductivity (EZO-EC)

**Dry Calibration** (required first):
1. Remove probe, let dry
2. Web UI → Calibrate → Dry

**Single Point** (±2% accuracy):
1. Submerge in 1413 µS/cm solution
2. Web UI → Calibrate → Single Point → 1413

**Two Point** (±1% accuracy):
1. Low point: 84 or 12,880 µS/cm solution
2. High point: 1413 or 80,000 µS/cm solution

## Troubleshooting

### Sensor not detected
- Check I2C wiring (SDA, SCL, power, ground)
- Verify sensor I2C address with I2C scanner
- Ensure sensors are powered (3.3V or 5V depending on model)

### Compilation errors
- Verify ArduinoJson library is installed (v7.x)
- Check ESP32 board support is installed
- Ensure all files are in correct folder structure

### No serial output
- Check baud rate is set to 115200
- Press reset button on ESP32
- Try different USB cable/port

## Project Structure

```
SeaSenseLogger/
├── SeaSenseLogger.ino          # Main entry point
├── config/
│   ├── hardware_config.h       # Pin definitions, I2C, feature flags
│   ├── device_config.h         # JSON device metadata
│   └── secrets.h               # WiFi, API keys (git-ignored)
├── src/
│   ├── sensors/
│   │   ├── SensorInterface.h   # Abstract sensor interface
│   │   ├── EZOSensor.h/.cpp    # Base class for EZO sensors
│   │   ├── EZO_RTD.h/.cpp      # Temperature sensor
│   │   ├── EZO_EC.h/.cpp       # Conductivity sensor
│   │   └── NMEA2000Environment.h/.cpp  # NMEA2000 data capture
│   ├── storage/
│   │   ├── StorageInterface.h  # Abstract storage interface
│   │   ├── SPIFFSStorage.h/.cpp
│   │   ├── SDStorage.h/.cpp
│   │   └── StorageManager.h/.cpp
│   ├── api/
│   │   └── APIUploader.h/.cpp  # Cloud upload with gzip compression
│   ├── calibration/
│   │   └── CalibrationManager.h/.cpp
│   ├── config/
│   │   └── ConfigManager.h/.cpp
│   ├── system/
│   │   └── SystemHealth.h/.cpp # Boot-loop detection, safe mode
│   └── webui/
│       └── WebServer.h/.cpp    # Web UI + REST API
└── test/
    ├── Makefile                # Native test runner (make test)
    ├── test_config_clamp.cpp
    ├── test_csv_roundtrip.cpp
    ├── test_millis_to_utc.cpp
    └── test_system_health.cpp
```

## Contributing

This logger is part of [Project SeaSense](https://projectseasense.org/) - a global network of sailors helping scientists monitor ocean health.

## License

ISC License (see main repository)
