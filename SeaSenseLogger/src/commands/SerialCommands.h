/**
 * SeaSense Logger - Serial Commands
 *
 * Interactive serial command interface for diagnostics and data management
 * Commands: DUMP, CLEAR, STATUS, TEST
 */

#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

#include <Arduino.h>
#include "../sensors/EZO_RTD.h"
#include "../sensors/EZO_EC.h"
#include "../sensors/GPSModule.h"
#include "../storage/StorageManager.h"
#include "../api/APIUploader.h"
#include "../pump/PumpController.h"

// Forward declaration
class SeaSenseWebServer;

class SerialCommands {
public:
    /**
     * Constructor
     * @param tempSensor Pointer to temperature sensor
     * @param ecSensor Pointer to conductivity sensor
     * @param gpsModule Pointer to GPS module
     * @param storage Pointer to storage manager
     * @param apiUploader Pointer to API uploader
     * @param webServer Pointer to web server
     * @param pumpController Pointer to pump controller
     */
    SerialCommands(
        EZO_RTD* tempSensor,
        EZO_EC* ecSensor,
        GPSModule* gpsModule,
        StorageManager* storage,
        APIUploader* apiUploader,
        SeaSenseWebServer* webServer,
        PumpController* pumpController
    );

    /**
     * Process serial input
     * Call this in loop()
     */
    void process();

private:
    EZO_RTD* _tempSensor;
    EZO_EC* _ecSensor;
    GPSModule* _gpsModule;
    StorageManager* _storage;
    APIUploader* _apiUploader;
    SeaSenseWebServer* _webServer;
    PumpController* _pumpController;

    String _commandBuffer;

    /**
     * Process a complete command
     * @param command Command string
     */
    void processCommand(const String& command);

    /**
     * DUMP - Output CSV data to serial
     */
    void cmdDump();

    /**
     * CLEAR - Delete all data (requires confirmation)
     */
    void cmdClear();

    /**
     * STATUS - Show system status
     */
    void cmdStatus();

    /**
     * TEST - Read sensors without logging
     */
    void cmdTest();

    /**
     * HELP - Show available commands
     */
    void cmdHelp();

    /**
     * PUMP - Pump control commands
     */
    void cmdPump(const String& args);

    /**
     * Print formatted output
     */
    void printHeader(const String& title);
    void printSeparator();
};

#endif // SERIAL_COMMANDS_H
