/**
 * SeaSense Logger - Serial Commands Implementation
 */

#include "SerialCommands.h"
#include "../webui/WebServer.h"
#include <Wire.h>

// ============================================================================
// Constructor
// ============================================================================

SerialCommands::SerialCommands(
    EZO_RTD* tempSensor,
    EZO_EC* ecSensor,
    GPSModule* gpsModule,
    StorageManager* storage,
    APIUploader* apiUploader,
    SeaSenseWebServer* webServer,
    PumpController* pumpController
)
    : _tempSensor(tempSensor),
      _ecSensor(ecSensor),
      _gpsModule(gpsModule),
      _storage(storage),
      _apiUploader(apiUploader),
      _webServer(webServer),
      _pumpController(pumpController),
      _commandBuffer("")
{
}

// ============================================================================
// Public Methods
// ============================================================================

void SerialCommands::process() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (_commandBuffer.length() > 0) {
                processCommand(_commandBuffer);
                _commandBuffer = "";
            }
        } else {
            _commandBuffer += c;
        }
    }
}

// ============================================================================
// Private Methods
// ============================================================================

void SerialCommands::processCommand(const String& command) {
    String cmd = command;
    cmd.trim();
    cmd.toUpperCase();

    Serial.println();  // Blank line

    if (cmd == "DUMP") {
        cmdDump();
    } else if (cmd == "CLEAR") {
        cmdClear();
    } else if (cmd == "STATUS") {
        cmdStatus();
    } else if (cmd == "TEST") {
        cmdTest();
    } else if (cmd == "SCAN") {
        cmdScan();
    } else if (cmd.startsWith("PUMP")) {
        String args = "";
        int spaceIndex = cmd.indexOf(' ');
        if (spaceIndex > 0) {
            args = cmd.substring(spaceIndex + 1);
        }
        cmdPump(args);
    } else if (cmd == "HELP" || cmd == "?") {
        cmdHelp();
    } else if (cmd.length() > 0) {
        Serial.print("Unknown command: ");
        Serial.println(cmd);
        Serial.println("Type HELP for available commands");
    }
}

void SerialCommands::cmdDump() {
    printHeader("DATA DUMP");

    if (!_storage) {
        Serial.println("Storage not available");
        return;
    }

    // Get storage stats
    StorageStats stats = _storage->getStats();
    Serial.print("Total records: ");
    Serial.println(stats.totalRecords);
    Serial.println();

    // Read all records
    std::vector<DataRecord> records = _storage->readRecords(0, 10000);

    if (records.empty()) {
        Serial.println("No data available");
        return;
    }

    // Print CSV header
    Serial.println("millis,timestamp_utc,latitude,longitude,altitude,gps_sats,gps_hdop,sensor_type,sensor_model,sensor_serial,sensor_instance,calibration_date,value,unit,quality");

    // Print records
    for (const DataRecord& record : records) {
        Serial.print(record.millis);
        Serial.print(",");
        Serial.print(record.timestampUTC);
        Serial.print(",");
        Serial.print(record.latitude, 6);
        Serial.print(",");
        Serial.print(record.longitude, 6);
        Serial.print(",");
        Serial.print(record.altitude, 1);
        Serial.print(",");
        Serial.print(record.gps_satellites);
        Serial.print(",");
        Serial.print(record.gps_hdop, 1);
        Serial.print(",");
        Serial.print(record.sensorType);
        Serial.print(",");
        Serial.print(record.sensorModel);
        Serial.print(",");
        Serial.print(record.sensorSerial);
        Serial.print(",");
        Serial.print(record.sensorInstance);
        Serial.print(",");
        Serial.print(record.calibrationDate);
        Serial.print(",");
        Serial.print(record.value, 2);
        Serial.print(",");
        Serial.print(record.unit);
        Serial.print(",");
        Serial.println(record.quality);
    }

    Serial.println();
    Serial.print("Dumped ");
    Serial.print(records.size());
    Serial.println(" records");
}

void SerialCommands::cmdClear() {
    printHeader("CLEAR DATA");

    Serial.println("WARNING: This will delete ALL stored data!");
    Serial.println("Type YES to confirm:");

    // Wait for confirmation (10 second timeout)
    unsigned long startTime = millis();
    String confirmation = "";

    while (millis() - startTime < 10000) {
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                break;
            }
            confirmation += c;
        }
    }

    confirmation.trim();

    if (confirmation == "YES") {
        Serial.println("Clearing data...");
        if (_storage && _storage->clear()) {
            Serial.println("Data cleared successfully");
        } else {
            Serial.println("Failed to clear data");
        }
    } else {
        Serial.println("Clear cancelled (confirmation not received)");
    }
}

void SerialCommands::cmdStatus() {
    printHeader("SYSTEM STATUS");

    // Uptime
    Serial.print("Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
    Serial.println();

    // Sensors
    printSeparator();
    Serial.println("SENSORS:");
    printSeparator();

    if (_tempSensor) {
        Serial.println(_tempSensor->getStatusString());
    }

    if (_ecSensor) {
        Serial.println(_ecSensor->getStatusString());
        Serial.print("Salinity: ");
        Serial.print(_ecSensor->getSalinity(), 2);
        Serial.println(" PSU");
    }
    Serial.println();

    // GPS
    if (_gpsModule) {
        printSeparator();
        Serial.println("GPS:");
        printSeparator();

        Serial.print("Status: ");
        Serial.println(_gpsModule->getStatusString());

        if (_gpsModule->hasValidFix()) {
            GPSData gpsData = _gpsModule->getData();
            Serial.print("Location: ");
            Serial.print(gpsData.latitude, 6);
            Serial.print("° N, ");
            Serial.print(gpsData.longitude, 6);
            Serial.println("° E");
            Serial.print("Altitude: ");
            Serial.print(gpsData.altitude, 1);
            Serial.println(" m");
            Serial.print("Time (UTC): ");
            Serial.println(_gpsModule->getTimeUTC());
            Serial.print("Satellites: ");
            Serial.println(gpsData.satellites);
            Serial.print("HDOP: ");
            Serial.println(gpsData.hdop, 1);
        }
        Serial.println();
    }

    // Storage
    if (_storage) {
        printSeparator();
        Serial.println("STORAGE:");
        printSeparator();

        Serial.println(_storage->getStatusString());

        StorageStats stats = _storage->getStats();
        Serial.print("Total records: ");
        Serial.println(stats.totalRecords);

        if (_storage->isSDMounted()) {
            Serial.print("SD card: ");
            Serial.print(stats.usedBytes / (1024 * 1024));
            Serial.print(" MB used / ");
            Serial.print(stats.totalBytes / (1024 * 1024));
            Serial.println(" MB total");
        }

        if (_storage->isSPIFFSMounted()) {
            StorageStats spiffsStats = _storage->getSPIFFSStats();
            Serial.print("SPIFFS: ");
            Serial.print(spiffsStats.usedBytes / 1024);
            Serial.print(" KB used / ");
            Serial.print(spiffsStats.totalBytes / 1024);
            Serial.println(" KB total");
        }
        Serial.println();
    }

    // WiFi
    if (_webServer) {
        printSeparator();
        Serial.println("NETWORK:");
        printSeparator();

        Serial.print("WiFi Status: ");
        Serial.println(_webServer->getWiFiStatus());

        Serial.print("AP IP: http://");
        Serial.println(_webServer->getAPIP());

        if (_webServer->isWiFiConnected()) {
            Serial.print("Station IP: http://");
            Serial.println(_webServer->getStationIP());
        }
        Serial.println();
    }

    // API Upload
    if (_apiUploader) {
        printSeparator();
        Serial.println("API UPLOAD:");
        printSeparator();

        Serial.print("Status: ");
        Serial.println(_apiUploader->getStatusString());

        Serial.print("Time synced: ");
        Serial.println(_apiUploader->isTimeSynced() ? "Yes" : "No");

        Serial.print("Pending records: ");
        Serial.println(_apiUploader->getPendingRecords());

        unsigned long nextUpload = _apiUploader->getTimeUntilNext();
        if (nextUpload > 0) {
            Serial.print("Next upload in: ");
            Serial.print(nextUpload / 1000);
            Serial.println(" seconds");
        }
        Serial.println();
    }

    // Pump
    if (_pumpController) {
        printSeparator();
        Serial.println("PUMP:");
        printSeparator();

        Serial.print("State: ");
        Serial.println(_pumpController->getStatusString());

        Serial.print("Enabled: ");
        Serial.println(_pumpController->isEnabled() ? "Yes" : "No");

        Serial.print("Relay: ");
        Serial.println(_pumpController->isRelayOn() ? "ON" : "OFF");

        Serial.print("Cycle progress: ");
        Serial.print(_pumpController->getCycleElapsed() / 1000);
        Serial.print("s / ");
        Serial.print(_pumpController->getCycleInterval() / 1000);
        Serial.println("s");

        String lastError = _pumpController->getLastError();
        if (lastError.length() > 0) {
            Serial.print("Last error: ");
            Serial.println(lastError);
        }

        Serial.println();
    }

    printSeparator();
}

void SerialCommands::cmdTest() {
    printHeader("SENSOR TEST");

    Serial.println("Reading sensors (no logging)...");
    Serial.println();

    // Test temperature
    if (_tempSensor && _tempSensor->isEnabled()) {
        Serial.print("Reading temperature...");
        if (_tempSensor->read()) {
            SensorData data = _tempSensor->getData();
            Serial.println(" OK");
            Serial.print("  Value: ");
            Serial.print(data.value, 2);
            Serial.print(" ");
            Serial.println(data.unit);
            Serial.print("  Quality: ");
            Serial.println(sensorQualityToString(data.quality));
        } else {
            Serial.println(" FAILED");
        }
    }

    Serial.println();

    // Test conductivity
    if (_ecSensor && _ecSensor->isEnabled()) {
        Serial.print("Reading conductivity...");
        if (_ecSensor->read()) {
            SensorData data = _ecSensor->getData();
            Serial.println(" OK");
            Serial.print("  Value: ");
            Serial.print(data.value, 0);
            Serial.print(" ");
            Serial.println(data.unit);
            Serial.print("  Quality: ");
            Serial.println(sensorQualityToString(data.quality));
            Serial.print("  Salinity: ");
            Serial.print(_ecSensor->getSalinity(), 2);
            Serial.println(" PSU");
        } else {
            Serial.println(" FAILED");
        }
    }

    Serial.println();
    Serial.println("Test complete");
}

void SerialCommands::cmdScan() {
    printHeader("I2C BUS SCANNER");

    Serial.println("Scanning I2C bus (0x01 - 0x7F)...");
    Serial.println();

    int devicesFound = 0;

    Serial.println("Address  Device");
    Serial.println("-------  ------------------");

    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            // Device found
            Serial.print("0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            Serial.print("   ");

            // Identify known devices
            if (address == 0x66) {
                Serial.println("EZO-RTD (Temperature)");
            } else if (address == 0x64) {
                Serial.println("EZO-EC (Conductivity)");
            } else if (address == 0x61) {
                Serial.println("EZO-DO (Dissolved Oxygen)");
            } else if (address == 0x63) {
                Serial.println("EZO-pH");
            } else {
                Serial.println("Unknown device");
            }

            devicesFound++;
        } else if (error == 4) {
            Serial.print("0x");
            if (address < 16) Serial.print("0");
            Serial.print(address, HEX);
            Serial.println("   ERROR");
        }
    }

    Serial.println();
    if (devicesFound == 0) {
        Serial.println("No I2C devices found!");
        Serial.println();
        Serial.println("Troubleshooting tips:");
        Serial.println("1. Check sensor power connections (5V and GND)");
        Serial.println("2. Verify I2C wiring (SDA on GPIO21, SCL on GPIO22)");
        Serial.println("3. Check for loose connections");
        Serial.println("4. Verify sensors are powered on (LED should be lit)");
        Serial.println("5. Try different I2C pull-up resistors (4.7kΩ typical)");
    } else {
        Serial.print("Found ");
        Serial.print(devicesFound);
        Serial.println(" device(s)");
        Serial.println();
        Serial.println("Expected devices:");
        Serial.println("  0x66 - EZO-RTD (Temperature sensor)");
        Serial.println("  0x64 - EZO-EC (Conductivity sensor)");
    }
}

void SerialCommands::cmdHelp() {
    printHeader("AVAILABLE COMMANDS");

    Serial.println("DUMP         - Output all CSV data to serial console");
    Serial.println("CLEAR        - Delete all stored data (requires YES confirmation)");
    Serial.println("STATUS       - Display system status and diagnostics");
    Serial.println("TEST         - Read sensors without logging");
    Serial.println("SCAN         - Scan I2C bus for connected devices");
    Serial.println("PUMP STATUS  - Display pump controller status");
    Serial.println("PUMP START   - Manually start pump cycle");
    Serial.println("PUMP STOP    - Emergency stop pump");
    Serial.println("PUMP PAUSE   - Pause pump cycles");
    Serial.println("PUMP RESUME  - Resume pump cycles");
    Serial.println("PUMP ENABLE  - Enable pump controller");
    Serial.println("PUMP DISABLE - Disable pump controller");
    Serial.println("HELP         - Show this help message");
    Serial.println();
    Serial.println("Type any command and press Enter");
}

void SerialCommands::cmdPump(const String& args) {
    if (!_pumpController) {
        Serial.println("Pump controller not available");
        return;
    }

    if (args == "STATUS" || args.length() == 0) {
        printHeader("PUMP STATUS");

        Serial.print("State: ");
        Serial.println(_pumpController->getStatusString());

        Serial.print("Enabled: ");
        Serial.println(_pumpController->isEnabled() ? "Yes" : "No");

        Serial.print("Relay: ");
        Serial.println(_pumpController->isRelayOn() ? "ON" : "OFF");

        Serial.print("Cycle progress: ");
        Serial.print(_pumpController->getCycleProgress());
        Serial.println("%");

        Serial.print("Time in cycle: ");
        Serial.print(_pumpController->getCycleElapsed() / 1000);
        Serial.print("s / ");
        Serial.print(_pumpController->getCycleInterval() / 1000);
        Serial.println("s");

        const PumpConfig& config = _pumpController->getConfig();
        Serial.println();
        Serial.println("Configuration:");
        Serial.print("  Relay Pin: GPIO ");
        Serial.println(config.relayPin);
        Serial.print("  Cycle Interval: ");
        Serial.print(config.cycleIntervalMs / 1000);
        Serial.println("s");
        Serial.print("  Startup Delay: ");
        Serial.print(config.pumpStartupDelayMs);
        Serial.println("ms");
        Serial.print("  Stability Wait: ");
        Serial.print(config.stabilityWaitMs);
        Serial.println("ms");
        Serial.print("  Measurements: ");
        Serial.println(config.measurementCount);
        Serial.print("  Max On Time: ");
        Serial.print(config.maxPumpOnTimeMs / 1000);
        Serial.println("s");

        String lastError = _pumpController->getLastError();
        if (lastError.length() > 0) {
            Serial.println();
            Serial.print("Last error: ");
            Serial.println(lastError);
        }
    }
    else if (args == "START") {
        Serial.println("Starting pump cycle...");
        _pumpController->startPump();
    }
    else if (args == "STOP") {
        Serial.println("Emergency stop - stopping pump...");
        _pumpController->stopPump();
    }
    else if (args == "PAUSE") {
        Serial.println("Pausing pump controller...");
        _pumpController->pause();
    }
    else if (args == "RESUME") {
        Serial.println("Resuming pump controller...");
        _pumpController->resume();
    }
    else if (args == "ENABLE") {
        Serial.println("Enabling pump controller...");
        _pumpController->setEnabled(true);
    }
    else if (args == "DISABLE") {
        Serial.println("Disabling pump controller...");
        _pumpController->setEnabled(false);
    }
    else {
        Serial.print("Unknown PUMP command: ");
        Serial.println(args);
        Serial.println("Available: STATUS, START, STOP, PAUSE, RESUME, ENABLE, DISABLE");
    }
}

void SerialCommands::printHeader(const String& title) {
    printSeparator();
    Serial.println(title);
    printSeparator();
}

void SerialCommands::printSeparator() {
    Serial.println("==================================================");
}
