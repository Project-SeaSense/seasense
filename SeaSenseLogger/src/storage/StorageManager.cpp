/**
 * SeaSense Logger - Storage Manager Implementation
 */

#include "StorageManager.h"
#include "../../config/hardware_config.h"
#include "../system/SystemHealth.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

StorageManager::StorageManager(uint16_t spiffsMaxRecords, uint8_t sdCsPin)
    : _spiffsAvailable(false),
      _sdAvailable(false)
{
    _spiffs = new SPIFFSStorage(spiffsMaxRecords);
    _sd = new SDStorage(sdCsPin);
}

StorageManager::~StorageManager() {
    delete _spiffs;
    delete _sd;
}

// ============================================================================
// Public Methods
// ============================================================================

bool StorageManager::begin() {
    Serial.println("\n[STORAGE] Initializing storage systems...");

    // Initialize SPIFFS
    _spiffsAvailable = _spiffs->begin();
    if (_spiffsAvailable) {
        Serial.println("[STORAGE] SPIFFS initialized successfully");
        StorageStats spiffsStats = _spiffs->getStats();
        Serial.print("[STORAGE] SPIFFS: ");
        Serial.print(spiffsStats.usedBytes / 1024);
        Serial.print(" KB used / ");
        Serial.print(spiffsStats.totalBytes / 1024);
        Serial.print(" KB total, ");
        Serial.print(spiffsStats.totalRecords);
        Serial.println(" records");
    } else {
        Serial.println("[STORAGE] SPIFFS initialization failed");
    }

    // Initialize SD card
    _sdAvailable = _sd->begin();
    if (_sdAvailable) {
        Serial.println("[STORAGE] SD card initialized successfully");
        StorageStats sdStats = _sd->getStats();
        Serial.print("[STORAGE] SD card: ");
        Serial.print(sdStats.usedBytes / (1024 * 1024));
        Serial.print(" MB used / ");
        Serial.print(sdStats.totalBytes / (1024 * 1024));
        Serial.print(" MB total, ");
        Serial.print(sdStats.totalRecords);
        Serial.println(" records");
    } else {
        Serial.println("[STORAGE] SD card initialization failed");
    }

    // Check if at least one storage system is available
    if (!_spiffsAvailable && !_sdAvailable) {
        Serial.println("[ERROR] No storage systems available!");
        return false;
    }

    Serial.print("[STORAGE] Storage ready - Primary: ");
    Serial.println(_sdAvailable ? "SD card" : "SPIFFS");

    return true;
}

bool StorageManager::write(const SensorData& data) {
    DataRecord record = sensorDataToRecord(data);
    return writeRecord(record);
}

bool StorageManager::writeRecord(const DataRecord& record) {
    bool success = false;

    // Write to SD card (primary)
    if (_sdAvailable) {
        if (_sd->writeRecord(record)) {
            success = true;
            DEBUG_STORAGE_PRINTLN("Written to SD card");
        } else {
            Serial.println("[STORAGE] SD write failed, attempting remount...");
            _sdAvailable = _sd->begin();
            if (_sdAvailable) {
                Serial.println("[STORAGE] SD remounted, retrying write...");
                if (_sd->writeRecord(record)) {
                    success = true;
                    DEBUG_STORAGE_PRINTLN("Written to SD card after remount");
                } else {
                    Serial.println("[STORAGE] SD write failed after remount");
                    extern SystemHealth systemHealth;
                    systemHealth.recordError(ErrorType::SD);
                }
            } else {
                Serial.println("[STORAGE] SD remount failed");
                extern SystemHealth systemHealth;
                systemHealth.recordError(ErrorType::SD);
            }
        }
    } else {
        // Periodically try to remount SD (may have been reinserted)
        static unsigned long lastSDRemountAttempt = 0;
        if (millis() - lastSDRemountAttempt > 30000) {  // Every 30 seconds
            lastSDRemountAttempt = millis();
            _sdAvailable = _sd->begin();
            if (_sdAvailable) {
                Serial.println("[STORAGE] SD card detected and remounted!");
                if (_sd->writeRecord(record)) {
                    success = true;
                }
            }
        }
    }

    // Write to SPIFFS (secondary/backup)
    if (_spiffsAvailable) {
        if (_spiffs->writeRecord(record)) {
            success = true;
            DEBUG_STORAGE_PRINTLN("Written to SPIFFS");
        } else {
            Serial.println("[STORAGE] Warning: SPIFFS write failed");
        }
    }

    if (!success) {
        Serial.println("[ERROR] Failed to write to any storage system");
    }

    return success;
}

std::vector<DataRecord> StorageManager::readRecords(
    unsigned long startMillis,
    uint16_t maxRecords,
    uint32_t skipRecords
) {
    // Read from primary storage
    IStorage* primary = getPrimaryStorage();
    if (primary) {
        return primary->readRecords(startMillis, maxRecords, skipRecords);
    }

    // No storage available
    return std::vector<DataRecord>();
}

StorageStats StorageManager::getStats() const {
    StorageStats stats;

    // Use primary storage stats
    IStorage* primary = getPrimaryStorage();
    if (primary) {
        stats = primary->getStats();
    } else {
        stats.mounted = false;
        stats.totalBytes = 0;
        stats.usedBytes = 0;
        stats.freeBytes = 0;
        stats.totalRecords = 0;
        stats.recordsSinceUpload = 0;
        stats.status = StorageStatus::NOT_MOUNTED;
    }

    return stats;
}

StorageStatus StorageManager::getStatus() const {
    // If SD is available, use its status
    if (_sdAvailable) {
        return _sd->getStatus();
    }

    // If only SPIFFS is available, use its status
    if (_spiffsAvailable) {
        return _spiffs->getStatus();
    }

    // No storage available
    return StorageStatus::NOT_MOUNTED;
}

bool StorageManager::clear() {
    bool success = false;

    Serial.println("[STORAGE] Clearing all data...");

    if (_sdAvailable) {
        if (_sd->clear()) {
            Serial.println("[STORAGE] SD card cleared");
            success = true;
        }
    }

    if (_spiffsAvailable) {
        if (_spiffs->clear()) {
            Serial.println("[STORAGE] SPIFFS cleared");
            success = true;
        }
    }

    return success;
}

unsigned long StorageManager::getLastUploadedMillis() const {
    // SPIFFS is the primary upload tracker
    if (_spiffsAvailable) {
        return _spiffs->getLastUploadedMillis();
    }

    // Fallback to SD if SPIFFS not available
    if (_sdAvailable) {
        return _sd->getLastUploadedMillis();
    }

    return 0;
}

bool StorageManager::setLastUploadedMillis(unsigned long millis) {
    bool success = false;

    // Update SPIFFS (primary upload tracker)
    if (_spiffsAvailable) {
        success = _spiffs->setLastUploadedMillis(millis);
    }

    // Also update SD for redundancy
    if (_sdAvailable) {
        _sd->setLastUploadedMillis(millis);
    }

    return success;
}

bool StorageManager::isSPIFFSMounted() const {
    return _spiffsAvailable && _spiffs->isMounted();
}

bool StorageManager::isSDMounted() const {
    return _sdAvailable && _sd->isMounted();
}

StorageStats StorageManager::getSPIFFSStats() const {
    if (_spiffsAvailable) {
        return _spiffs->getStats();
    }

    StorageStats stats;
    stats.mounted = false;
    stats.status = StorageStatus::NOT_MOUNTED;
    return stats;
}

StorageStats StorageManager::getSDStats() const {
    if (_sdAvailable) {
        return _sd->getStats();
    }

    StorageStats stats;
    stats.mounted = false;
    stats.status = StorageStatus::NOT_MOUNTED;
    return stats;
}

void StorageManager::addBytesUploaded(size_t bytes) {
    if (_spiffsAvailable) {
        _spiffs->addBytesUploaded(bytes);
    }
}

uint64_t StorageManager::getTotalBytesUploaded() const {
    if (_spiffsAvailable) {
        return _spiffs->getTotalBytesUploaded();
    }
    return 0;
}

String StorageManager::getStatusString() const {
    String status = "";

    if (_sdAvailable) {
        status += "SD: " + storageStatusToString(_sd->getStatus());
    } else {
        status += "SD: Not Available";
    }

    status += ", ";

    if (_spiffsAvailable) {
        status += "SPIFFS: " + storageStatusToString(_spiffs->getStatus());
    } else {
        status += "SPIFFS: Not Available";
    }

    return status;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

IStorage* StorageManager::getPrimaryStorage() const {
    // SD card is primary if available
    if (_sdAvailable) {
        return _sd;
    }

    // SPIFFS is fallback
    if (_spiffsAvailable) {
        return _spiffs;
    }

    return nullptr;
}

IStorage* StorageManager::getSecondaryStorage() const {
    // If SD is primary, SPIFFS is secondary
    if (_sdAvailable && _spiffsAvailable) {
        return _spiffs;
    }

    return nullptr;
}
