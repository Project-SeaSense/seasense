# SeaSense Logger

ESP32-based water quality logger for Atlas Scientific EZO sensors. Logs temperature, conductivity, pH, and dissolved oxygen with full sensor metadata traceability. Supports NMEA2000 for GPS and environmental context from boat instruments.

## Hardware Requirements

- **ESP32-S3 DevKit** (or compatible)
- **Atlas Scientific EZO-RTD** - Temperature sensor, I2C 0x66 (optional)
- **Atlas Scientific EZO-EC** - Conductivity sensor, I2C 0x64 (optional)
- **Atlas Scientific EZO-pH** - pH sensor, I2C 0x63 (optional)
- **Atlas Scientific EZO-DO** - Dissolved oxygen sensor, I2C 0x61 (optional)
- **NEO-6M GPS module** - For self-reliant time and location (UART2)
- **CAN bus transceiver** - For NMEA2000 network (e.g., SN65HVD230)
- **SD card module** - For permanent data storage
- **Pump relay module** - For water flow control during measurements
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
     - **NMEA2000** by Timo Lappalainen — for NMEA2000 bus communication
     - **N2kMessages** — NMEA2000 message definitions

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

## Current Status

### Implemented
- EZO-RTD temperature sensor with single-point calibration
- EZO-EC conductivity sensor with temperature compensation and multi-point calibration
- EZO-pH sensor with 3-point calibration (mid/low/high per Atlas Scientific specs)
- EZO-DO dissolved oxygen sensor with atmospheric and zero calibration
- Salinity calculation (simplified PSS-78) from conductivity
- Quality assessment framework for all sensor types
- Pump controller with 3-state cycle (flush/measure/idle) and safety cutoff
- Dual storage (SPIFFS circular buffer + SD card permanent) with power-loss protection
- GPS module integration (NEO-6M) for self-reliant time and location
- NMEA2000 GPS and environment data capture (wind, depth, heading, attitude, atmosphere)
- Web UI for real-time monitoring, calibration, configuration, and data management
- API upload with bandwidth management, gzip compression, and retry with exponential backoff
- Upload progress tracking (records since last upload)
- Serial command interface (DUMP, CLEAR, STATUS, TEST)
- Safe mode boot-loop detection and system health monitoring
- All sensors degrade gracefully when not present (auto-detected at startup)

## Features

### Sensor Capabilities
All sensors are optional — the system auto-detects connected hardware at startup and operates with whatever is available.

- **Temperature** (EZO-RTD): -126°C to +1254°C, ±0.1°C accuracy
- **Conductivity** (EZO-EC): 0.07 to 500,000 µS/cm, ±1-2% accuracy
- **pH** (EZO-pH): 0.001–14.000, ±0.002 accuracy, 3-point calibration
- **Dissolved Oxygen** (EZO-DO): 0.01–100+ mg/L, ±0.05 mg/L accuracy
- **Salinity**: Calculated from conductivity and temperature (PSS-78)
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
- Real-time sensor monitoring dashboard with live readings
- pH and DO sensor cards auto-appear when connected, show "Not Connected" when absent
- Live NMEA2000 environment data (wind, depth, heading, atmosphere, attitude)
- Guided calibration workflow for all four sensor types
- Pump controller status and configuration
- Data viewing, download, and upload history
- Device and network configuration

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
GPS [NEO]: 52.374500° N, 4.889500° E (8 sats, HDOP: 1.2)
GPS Time: 2024-05-15T10:28:23Z
Temperature: 18.50 °C [GOOD]
Conductivity: 42500 µS/cm [GOOD]
Salinity: 34.52 PSU
pH: 8.12 pH [GOOD]
Dissolved Oxygen: 7.85 mg/L [GOOD]
```

## Pin Assignments

See `config/hardware_config.h` for complete pin configuration:

- **I2C**: SDA=8, SCL=9 (ESP32-S3 defaults)
- **GPS**: RX=16, TX=17 (UART2)
- **SD Card**: CS=5, MOSI=23, MISO=19, SCK=18
- **Status LED**: GPIO 2
- **Pump Relay**: GPIO 25
- **CAN Bus** (NMEA2000): TX=4, RX=27

## Calibration

All calibration is done via the web UI at `/calibrate`. The UI auto-detects which sensors are connected and shows calibration cards only for present sensors.

### Temperature (EZO-RTD)
Usually factory calibrated - no field calibration needed. Single-point calibration available via web UI if needed.

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

### pH (EZO-pH)

Always start with mid-point. Rinse probe between solutions.

**Mid Point** (required first):
1. Submerge in pH 7.00 buffer
2. Web UI → Calibrate → Mid Point

**Low Point** (optional, improves accuracy):
1. Submerge in pH 4.00 buffer
2. Web UI → Calibrate → Low Point

**High Point** (optional, for full 3-point):
1. Submerge in pH 10.00 buffer
2. Web UI → Calibrate → High Point

### Dissolved Oxygen (EZO-DO)

**Atmospheric** (required):
1. Hold probe in air, ensure membrane is dry
2. Web UI → Calibrate → Atmospheric

**Zero** (optional, improves low-range accuracy):
1. Submerge in sodium sulfite (Na2SO3) solution
2. Web UI → Calibrate → Zero

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
│   ├── hardware_config.h       # Pin definitions, I2C addresses, timing
│   ├── device_config.h         # JSON device metadata
│   └── secrets.h               # WiFi, API keys (git-ignored)
├── scripts/
│   └── build.sh                # Arduino CLI build helper
├── src/
│   ├── sensors/
│   │   ├── SensorInterface.h   # Abstract sensor interface
│   │   ├── EZOSensor.h/.cpp    # Base class for EZO sensors
│   │   ├── EZO_RTD.h/.cpp      # Temperature sensor
│   │   ├── EZO_EC.h/.cpp       # Conductivity sensor
│   │   ├── EZO_pH.h/.cpp       # pH sensor
│   │   ├── EZO_DO.h/.cpp       # Dissolved oxygen sensor
│   │   ├── GPSModule.h/.cpp    # NEO-6M GPS
│   │   ├── NMEA2000GPS.h/.cpp  # NMEA2000 GPS source
│   │   └── NMEA2000Environment.h/.cpp  # NMEA2000 environment data
│   ├── storage/
│   │   ├── StorageInterface.h  # Abstract storage interface
│   │   ├── SPIFFSStorage.h/.cpp
│   │   ├── SDStorage.h/.cpp
│   │   └── StorageManager.h/.cpp
│   ├── api/
│   │   └── APIUploader.h/.cpp  # Cloud upload with gzip compression
│   ├── calibration/
│   │   └── CalibrationManager.h/.cpp  # Guided calibration for all sensors
│   ├── config/
│   │   └── ConfigManager.h/.cpp
│   ├── pump/
│   │   └── PumpController.h/.cpp  # 3-state pump cycle controller
│   ├── commands/
│   │   └── SerialCommands.h/.cpp  # Serial command interface
│   ├── system/
│   │   └── SystemHealth.h/.cpp # Boot-loop detection, safe mode
│   └── webui/
│       └── WebServer.h/.cpp    # Web UI + REST API
└── test/
    ├── Makefile                # Native test runner (make test)
    ├── test_config_clamp.cpp
    ├── test_csv_roundtrip.cpp
    ├── test_gps_nan_guard.cpp
    ├── test_metadata_batching.cpp
    ├── test_millis_to_utc.cpp
    ├── test_millis_rollover.cpp
    ├── test_upload_timing.cpp
    └── test_system_health.cpp
```

## Contributing

This logger is part of [Project SeaSense](https://projectseasense.org/) - a global network of sailors helping scientists monitor ocean health.

## License

ISC License (see main repository)
