/**
 * SeaSense Logger - OTA Firmware Update Manager
 *
 * Handles firmware updates via:
 * 1. Server check — polls GitHub Releases API, compares version, downloads .bin
 * 2. Manual upload — browser uploads .bin via chunked HTTP POST
 *
 * Safety: size check, MD5 (built into ESP32 Update.h), rollback protection
 */

#ifndef SEASENSE_OTA_MANAGER_H
#define SEASENSE_OTA_MANAGER_H

#include <Arduino.h>

#ifndef NATIVE_TEST
#include <Update.h>
#include <HTTPClient.h>
#endif

class OTAManager {
public:
    enum class State { IDLE, CHECKING, RECEIVING, SUCCESS, ERROR };

    struct UpdateInfo {
        bool available;
        String version;
        String url;
    };

    OTAManager();

    // Check GitHub Releases for updates (blocking HTTP call)
    UpdateInfo checkForUpdate(const String& currentVersion);

    // Manual upload (browser form — chunked)
    bool begin(size_t fileSize);
    bool writeChunk(const uint8_t* data, size_t length);
    bool end();

    // Server update (download from URL)
    bool updateFromUrl(const String& url);

    void abort();

    State getState() const { return _state; }
    String getErrorMessage() const { return _errorMessage; }
    uint8_t getProgress() const { return _progress; }
    size_t getMaxFirmwareSize() const;

    // Parse version from GitHub release tag (strip "fw-" prefix)
    static String parseVersionFromTag(const String& tag);

private:
    State _state;
    String _errorMessage;
    uint8_t _progress;
    size_t _totalSize;
    size_t _written;

    void setError(const String& message);
    void updateProgress();
};

#endif // SEASENSE_OTA_MANAGER_H
