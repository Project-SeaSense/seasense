/**
 * SeaSense Logger - Configuration Manager
 *
 * Runtime configuration management with SPIFFS persistence
 * Provides centralized config loading, saving, and defaults
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include "../pump/PumpController.h"

class ConfigManager {
public:
    /**
     * WiFi configuration
     */
    struct WiFiConfig {
        String stationSSID;
        String stationPassword;
        String apPassword;
    };

    /**
     * API configuration
     */
    struct APIConfig {
        String url;
        String apiKey;
        uint32_t uploadInterval;  // milliseconds
        uint8_t batchSize;
        uint8_t maxRetries;
    };

    /**
     * Device configuration
     */
    struct DeviceConfig {
        String deviceGUID;
        String partnerID;
        String firmwareVersion;
    };

    /**
     * Sampling configuration
     */
    struct SamplingConfig {
        uint32_t sensorIntervalMs;  // milliseconds between sensor readings
    };

    /**
     * GPS source configuration
     */
    struct GPSConfig {
        bool useNMEA2000;       // false = onboard GPS, true = NMEA2000 network
        bool fallbackToOnboard; // fall back to onboard GPS if NMEA2000 has no fix
    };

    /**
     * Constructor
     */
    ConfigManager();

    /**
     * Initialize and load configuration from SPIFFS
     * @return true if successful
     */
    bool begin();

    /**
     * Save current configuration to SPIFFS
     * @return true if successful
     */
    bool save();

    /**
     * Reset configuration to defaults
     * @return true if successful
     */
    bool reset();

    /**
     * Get WiFi configuration
     * @return WiFiConfig struct
     */
    WiFiConfig getWiFiConfig() const;

    /**
     * Set WiFi configuration
     * @param config WiFiConfig struct
     */
    void setWiFiConfig(const WiFiConfig& config);

    /**
     * Get API configuration
     * @return APIConfig struct
     */
    APIConfig getAPIConfig() const;

    /**
     * Set API configuration
     * @param config APIConfig struct
     */
    void setAPIConfig(const APIConfig& config);

    /**
     * Get device configuration
     * @return DeviceConfig struct
     */
    DeviceConfig getDeviceConfig() const;

    /**
     * Set device configuration
     * @param config DeviceConfig struct
     */
    void setDeviceConfig(const DeviceConfig& config);

    /**
     * Get pump configuration
     * @return PumpConfig struct
     */
    PumpConfig getPumpConfig() const;

    /**
     * Set pump configuration
     * @param config PumpConfig struct
     */
    void setPumpConfig(const PumpConfig& config);

    /**
     * Get sampling configuration
     * @return SamplingConfig struct
     */
    SamplingConfig getSamplingConfig() const;

    /**
     * Set sampling configuration
     * @param config SamplingConfig struct
     */
    void setSamplingConfig(const SamplingConfig& config);

    /**
     * Get GPS source configuration
     * @return GPSConfig struct
     */
    GPSConfig getGPSConfig() const;

    /**
     * Set GPS source configuration
     * @param config GPSConfig struct
     */
    void setGPSConfig(const GPSConfig& config);

private:
    static const char* CONFIG_FILE;  // "/settings.json"

    WiFiConfig _wifi;
    APIConfig _api;
    DeviceConfig _device;
    PumpConfig _pump;
    SamplingConfig _sampling;
    GPSConfig _gps;

    /**
     * Load configuration from SPIFFS file
     * @return true if successful
     */
    bool loadFromFile();

    /**
     * Save configuration to SPIFFS file
     * @return true if successful
     */
    bool saveToFile();

    /**
     * Set default values from compile-time defines
     */
    void setDefaults();
};

#endif // CONFIG_MANAGER_H
