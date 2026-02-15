# GPS Integration Plan

## Why GPS is Critical

**Current Limitation:** System depends on WiFi + NTP for timestamps
- NTP requires internet connection
- No absolute time without WiFi
- No location data
- Can't upload data without time sync

**GPS Solution:** Self-reliant time and location
- ✅ Accurate time without internet (atomic clock accuracy)
- ✅ Location for every datapoint (lat/lon)
- ✅ Works anywhere (no WiFi needed)
- ✅ Required for marine applications

---

## Hardware Options

### Option 1: NEO-6M GPS Module (Recommended)
- **Cost**: $10-15
- **Interface**: UART/Serial
- **Accuracy**: 2.5m CEP
- **Update Rate**: 1-10 Hz
- **Pins**: TX, RX, VCC (3.3V), GND
- **Library**: TinyGPS++

**Wiring:**
```
NEO-6M    →  ESP32
TX        →  GPIO 16 (RX2)
RX        →  GPIO 17 (TX2)
VCC       →  3.3V
GND       →  GND
```

### Option 2: NEO-M8N GPS Module (Higher Accuracy)
- **Cost**: $20-25
- **Accuracy**: Better GNSS (GPS + GLONASS + Galileo + BeiDou)
- **Same interface** as NEO-6M

### Option 3: NMEA2000 GPS (Future)
- Use existing NMEA2000 network GPS
- Listen for PGN 129029 (Position) and PGN 129033 (Time)
- No additional hardware needed if boat has GPS
- Requires CAN bus implementation first

---

## Implementation

### 1. GPS Module Class

**File:** `src/sensors/GPSModule.h`

```cpp
class GPSModule {
public:
    struct GPSData {
        bool valid;
        double latitude;
        double longitude;
        double altitude;
        uint16_t year;
        uint8_t month, day, hour, minute, second;
        time_t epoch;
        uint8_t satellites;
        double hdop;
    };

    GPSModule(uint8_t rxPin, uint8_t txPin);

    bool begin();
    void update();  // Call in loop()

    bool hasValidFix() const;
    GPSData getData() const;
    String getTimeUTC() const;  // ISO 8601 format

private:
    HardwareSerial* _serial;
    TinyGPSPlus _gps;
    GPSData _data;
};
```

### 2. Integration Points

#### A. Update DataRecord Structure
```cpp
struct DataRecord {
    unsigned long millis;
    String timestampUTC;      // From GPS
    double latitude;          // NEW
    double longitude;         // NEW
    double altitude;          // NEW
    uint8_t gps_satellites;   // NEW
    double gps_hdop;          // NEW
    String sensorType;
    String sensorModel;
    String sensorSerial;
    uint8_t sensorInstance;
    String calibrationDate;
    float value;
    String unit;
    String quality;
};
```

#### B. Update CSV Format
```csv
millis,timestamp_utc,latitude,longitude,altitude,gps_sats,gps_hdop,sensor_type,sensor_model,sensor_serial,sensor_instance,calibration_date,value,unit,quality
12345678,2024-05-15T10:28:23Z,52.3745,4.8895,0,8,1.2,Temperature,EZO-RTD,RTD-12345,1,2024-01-15,18.5,°C,good
```

#### C. Update API Payload
```json
{
  "datapoints": [
    {
      "timestamp_utc": "2024-05-15T10:28:23Z",
      "latitude": 52.3745,
      "longitude": 4.8895,
      "hdop": 1.2,
      "water_temperature_c": 18.5,
      "sensor_model": "EZO-RTD",
      ...
    }
  ]
}
```

### 3. Main Loop Integration

```cpp
GPSModule gps(16, 17);  // RX2, TX2

void setup() {
    // ... existing setup ...

    // Initialize GPS
    if (gps.begin()) {
        Serial.println("[GPS] GPS module initialized");
    } else {
        Serial.println("[GPS] GPS module not found");
    }
}

void loop() {
    // Update GPS (must be called frequently)
    gps.update();

    // ... existing sensor reading ...

    // When logging, add GPS data
    if (gps.hasValidFix()) {
        GPSData gpsData = gps.getData();
        record.timestampUTC = gps.getTimeUTC();
        record.latitude = gpsData.latitude;
        record.longitude = gpsData.longitude;
        record.altitude = gpsData.altitude;
        record.gps_satellites = gpsData.satellites;
        record.gps_hdop = gpsData.hdop;
    }
}
```

---

## Benefits

### 1. Self-Reliant Operation
- ✅ Works without WiFi
- ✅ Works without internet
- ✅ Works anywhere in the world
- ✅ Accurate time from satellites

### 2. Scientific Data Quality
- ✅ Every datapoint has lat/lon
- ✅ Traceable to specific location
- ✅ Track water properties along route
- ✅ Timestamped with atomic clock accuracy

### 3. Advanced Features Enabled
- 🗺️ **Track Plot**: Visualize sensor data on map
- 📊 **Spatial Analysis**: Temperature/salinity gradients
- 🌊 **Ocean Current Detection**: Speed over ground vs speed through water
- 📍 **Geo-fencing**: Auto-start/stop logging in regions

---

## Implementation Priority

### Phase 9: GPS Integration (High Priority)
1. Add NEO-6M GPS module hardware
2. Install TinyGPS++ library
3. Create GPSModule class
4. Update DataRecord with GPS fields
5. Update CSV format
6. Update API payload format
7. Test GPS fix and time accuracy

**Estimated Time:** 2-4 hours
**Hardware Cost:** $10-15 for NEO-6M module

---

## Fallback Strategy

**Graceful Degradation:**
- System works without GPS (uses millis() timestamps)
- GPS data added when available
- Empty lat/lon fields if no GPS fix
- API upload still works (API accepts sparse data)

**Display in Web UI:**
```
GPS Status: ✓ Fixed (8 satellites, HDOP: 1.2)
Location: 52.3745° N, 4.8895° E
Time: 2024-05-15 10:28:23 UTC
```

---

## Library Requirements

```cpp
// Install via Arduino Library Manager
#include <TinyGPSPlus.h>  // By Mikal Hart

// Hardware Serial
HardwareSerial GPS_Serial(2);  // Use UART2
```

---

## Testing Plan

1. **Indoor Test** (no GPS fix)
   - Verify system still works without GPS
   - Check empty lat/lon handling

2. **Outdoor Test** (GPS fix)
   - Verify GPS acquires fix (<1 minute)
   - Check time accuracy
   - Verify lat/lon accuracy
   - Test HDOP values

3. **Moving Test** (on boat)
   - Verify GPS updates while moving
   - Check data quality at speed
   - Test under GPS antenna obstruction

---

## Next Steps

**Would you like me to:**
1. ✅ **Implement GPS module now** - Add hardware, create class, integrate
2. ⏸️ **Finish current phases first** - Complete API upload + serial commands
3. 📝 **Create detailed GPS specification** - Pin diagrams, full code

GPS is critical for marine science - recommend implementing soon!
