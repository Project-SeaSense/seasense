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
        uint32_t sensorIntervalMs;     // milliseconds between sensor readings
        bool skipIfStationary;         // if true, skip storage measurement when GPS movement is minimal
        float stationaryDeltaMeters;   // minimum movement to count as significant
    };

    /**
     * GPS source configuration
     */
    struct GPSConfig {
        bool useNMEA2000;       // false = onboard GPS, true = NMEA2000 network
        bool fallbackToOnboard; // fall back to onboard GPS if NMEA2000 has no fix
    };

    /**
     * Deployment metadata
     */
    struct DeploymentConfig {
        String deployDate;      // ISO8601 - auto-set on first boot, persisted
        String purchaseDate;    // ISO8601 - set via web UI / API
        float depthCm;          // Sensor depth below waterline in cm
        // TODO: Add Web UI settings page for depthCm, purchaseDate, and deployDate
        //       (currently only deployDate is auto-stamped on first GPS fix;
        //        purchaseDate and depthCm have no UI to set them)
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

    /**
     * Get deployment metadata
     * @return DeploymentConfig struct
     */
    DeploymentConfig getDeploymentConfig() const;

    /**
     * Set deployment metadata
     * @param config DeploymentConfig struct
     */
    void setDeploymentConfig(const DeploymentConfig& config);

    /**
     * Set deploy_date to current UTC time if not already set.
     * Call from setup() after time sync.
     * @param utcTimestamp ISO8601 UTC string (e.g. from GPS)
     * @return true if deploy_date was set (first boot)
     */
    bool stampDeployDate(const String& utcTimestamp);

    /**
     * Generate a brand-new device GUID, persist it, and return it.
     * Call this when the user requests a fresh GUID via the web UI.
     */
    String regenerateDeviceGUID();

private:
    static const char* CONFIG_FILE;  // "/settings.json"

    WiFiConfig _wifi;
    APIConfig _api;
    DeviceConfig _device;
    PumpConfig _pump;
    SamplingConfig _sampling;
    GPSConfig _gps;
    DeploymentConfig _deployment;

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

    /**
     * Clamp all config values to safe operating bounds
     */
    void clampConfig();

    /**
     * Generate a UUID v4 with "seasense-" prefix using ESP32 hardware RNG
     */
    String generateDeviceGUID();

    /**
     * Ensure device GUID is set; generates one if empty or a known placeholder.
     * @return true if a new GUID was generated (caller should save)
     */
    bool ensureDeviceGUID();
};

#endif // CONFIG_MANAGER_H
