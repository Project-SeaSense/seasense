/**
 * SeaSense Logger - OTA Firmware Update Manager Implementation
 */

#include "OTAManager.h"

#ifndef NATIVE_TEST
#include <ArduinoJson.h>
#endif

OTAManager::OTAManager()
    : _state(State::IDLE),
      _progress(0),
      _totalSize(0),
      _written(0)
{
}

String OTAManager::parseVersionFromTag(const String& tag) {
    if (tag.startsWith("fw-")) {
        return tag.substring(3);
    }
    return tag;
}

size_t OTAManager::getMaxFirmwareSize() const {
#ifdef NATIVE_TEST
    return 1966080;  // 0x1E0000 = 1.875 MB (matches partition table)
#else
    return ESP.getFreeSketchSpace();
#endif
}

void OTAManager::setError(const String& message) {
    _state = State::ERROR;
    _errorMessage = message;
    Serial.print("[OTA] Error: ");
    Serial.println(message);
}

void OTAManager::updateProgress() {
    if (_totalSize > 0) {
        _progress = (uint8_t)((_written * 100) / _totalSize);
    }
}

// ============================================================================
// Check for updates via GitHub Releases API
// ============================================================================

OTAManager::UpdateInfo OTAManager::checkForUpdate(const String& currentVersion) {
    UpdateInfo info = {false, "", ""};

#ifndef NATIVE_TEST
    _state = State::CHECKING;
    Serial.println("[OTA] Checking for updates...");

    HTTPClient http;
    http.begin("https://api.github.com/repos/Project-SeaSense/seasense/releases/latest");
    http.addHeader("User-Agent", "SeaSense-ESP32");
    http.setTimeout(10000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        setError("GitHub API returned " + String(httpCode));
        http.end();
        return info;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON response
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        setError("JSON parse error: " + String(err.c_str()));
        return info;
    }

    String tagName = doc["tag_name"] | "";
    if (tagName.isEmpty()) {
        setError("No tag_name in release");
        return info;
    }

    String remoteVersion = parseVersionFromTag(tagName);

    // Find .bin asset
    JsonArray assets = doc["assets"];
    for (JsonObject asset : assets) {
        String name = asset["name"] | "";
        if (name.endsWith(".bin")) {
            info.url = asset["browser_download_url"] | "";
            break;
        }
    }

    if (info.url.isEmpty()) {
        setError("No .bin asset in release");
        return info;
    }

    // Compare versions
    if (currentVersion.isEmpty() || remoteVersion != currentVersion) {
        info.available = true;
        info.version = remoteVersion;
        Serial.print("[OTA] Update available: ");
        Serial.println(remoteVersion);
    } else {
        Serial.println("[OTA] Firmware is up to date");
    }

    _state = State::IDLE;
#endif

    return info;
}

// ============================================================================
// Manual upload — chunked writes from browser
// ============================================================================

bool OTAManager::begin(size_t fileSize) {
    size_t maxSize = getMaxFirmwareSize();
    if (fileSize > maxSize) {
        setError("Firmware too large: " + String((unsigned long)fileSize) + " bytes, max " + String((unsigned long)maxSize) + " bytes");
        return false;
    }

    _totalSize = fileSize;
    _written = 0;
    _progress = 0;
    _errorMessage = "";

#ifndef NATIVE_TEST
    if (!Update.begin(fileSize)) {
        setError("Update.begin() failed");
        return false;
    }
#endif

    _state = State::RECEIVING;
    Serial.print("[OTA] Begin upload, size: ");
    Serial.println(fileSize);
    return true;
}

bool OTAManager::writeChunk(const uint8_t* data, size_t length) {
    if (_state != State::RECEIVING) {
        return false;
    }

#ifndef NATIVE_TEST
    size_t written = Update.write(const_cast<uint8_t*>(data), length);
    if (written != length) {
        setError("Write failed: wrote " + String((unsigned long)written) + " of " + String((unsigned long)length));
        return false;
    }
#endif

    _written += length;
    updateProgress();
    return true;
}

bool OTAManager::end() {
    if (_state != State::RECEIVING) {
        return false;
    }

#ifndef NATIVE_TEST
    if (!Update.end(true)) {
        setError("Update.end() failed");
        return false;
    }
#endif

    _state = State::SUCCESS;
    _progress = 100;
    Serial.println("[OTA] Update complete, restarting...");
    return true;
}

// ============================================================================
// Server update — download .bin from URL and flash
// ============================================================================

bool OTAManager::updateFromUrl(const String& url) {
#ifndef NATIVE_TEST
    _state = State::RECEIVING;
    _written = 0;
    _progress = 0;
    _errorMessage = "";

    Serial.print("[OTA] Downloading from: ");
    Serial.println(url);

    HTTPClient http;
    http.begin(url);
    http.addHeader("User-Agent", "SeaSense-ESP32");
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(30000);

    int httpCode = http.GET();
    if (httpCode != 200) {
        setError("Download failed: HTTP " + String(httpCode));
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        setError("Invalid content length");
        http.end();
        return false;
    }

    size_t maxSize = getMaxFirmwareSize();
    if ((size_t)contentLength > maxSize) {
        setError("Firmware too large: " + String(contentLength) + " bytes, max " + String((unsigned long)maxSize) + " bytes");
        http.end();
        return false;
    }

    _totalSize = contentLength;

    if (!Update.begin(contentLength)) {
        setError("Update.begin() failed");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    while (_written < _totalSize) {
        size_t available = stream->available();
        if (available == 0) {
            delay(1);
            continue;
        }
        size_t toRead = (available < sizeof(buf)) ? available : sizeof(buf);
        size_t bytesRead = stream->readBytes(buf, toRead);
        if (bytesRead == 0) {
            setError("Stream read timeout");
            Update.abort();
            http.end();
            return false;
        }
        size_t written = Update.write(buf, bytesRead);
        if (written != bytesRead) {
            setError("Update write failed");
            Update.abort();
            http.end();
            return false;
        }
        _written += bytesRead;
        updateProgress();
    }

    if (!Update.end(true)) {
        setError("Update.end() failed");
        http.end();
        return false;
    }

    http.end();
    _state = State::SUCCESS;
    _progress = 100;
    Serial.println("[OTA] Update complete, restarting...");
    return true;
#else
    (void)url;
    return false;
#endif
}

// ============================================================================
// Abort
// ============================================================================

void OTAManager::abort() {
#ifndef NATIVE_TEST
    if (_state == State::RECEIVING) {
        Update.abort();
    }
#endif
    _state = State::IDLE;
    _progress = 0;
    _written = 0;
    _totalSize = 0;
    _errorMessage = "";
    Serial.println("[OTA] Aborted");
}
