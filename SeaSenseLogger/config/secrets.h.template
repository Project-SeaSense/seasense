/**
 * SeaSense Logger v2 - Secrets Configuration
 *
 * WiFi credentials and API keys
 *
 * ⚠️ IMPORTANT: This file should be added to .gitignore
 * Never commit credentials to version control
 *
 * Copy this file to secrets.h and fill in your actual credentials
 */

#ifndef SECRETS_H
#define SECRETS_H

// ============================================================================
// WiFi Station Mode Credentials
// ============================================================================

// Boat WiFi network credentials
// Leave empty to disable Station mode (AP mode only)
#define WIFI_STATION_SSID ""        // e.g., "BoatWiFi" or "Starlink"
#define WIFI_STATION_PASSWORD ""    // e.g., "your-wifi-password"

// ============================================================================
// Project SeaSense API Configuration
// ============================================================================

// API endpoint URL
// Use test API for development, production API for deployment
#define API_URL "https://test-api.projectseasense.org"  // Test environment
// #define API_URL "https://api.projectseasense.org"     // Production (uncomment when ready)

// API authentication key
// Get your API key from the SeaSense partner portal
#define API_KEY ""  // e.g., "sk_live_1234567890abcdef"

// ============================================================================
// NTP Time Sync Configuration
// ============================================================================

// NTP server for time synchronization
// Required for API uploads (timestamp_utc field)
#define NTP_SERVER "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC 0        // GMT+0 (UTC)
#define NTP_DAYLIGHT_OFFSET_SEC 0   // No daylight saving for UTC

// ============================================================================
// Optional: Custom Device Identification
// ============================================================================

// Override device GUID (optional)
// If empty, uses device_guid from device_config.h
#define CUSTOM_DEVICE_GUID ""

// Override partner ID (optional)
// If empty, uses partner_id from device_config.h
#define CUSTOM_PARTNER_ID ""

// ============================================================================
// Notes
// ============================================================================

/*
 * Setting up WiFi:
 * 1. Fill in WIFI_STATION_SSID and WIFI_STATION_PASSWORD with your boat's WiFi
 * 2. The device will attempt to connect to this network on boot
 * 3. If connection fails, it falls back to AP mode only
 * 4. AP mode is always available at 192.168.4.1
 *
 * Setting up API:
 * 1. Get your API key from Project SeaSense partner portal
 * 2. Fill in API_KEY
 * 3. Configure upload settings via web UI (http://192.168.4.1)
 * 4. Test with test API first before switching to production
 *
 * Time Sync:
 * - NTP sync happens automatically when WiFi connects
 * - If NTP sync fails, API uploads will be disabled
 * - Check web UI dashboard for time sync status
 * - In future, GPS time from NMEA2000 will override NTP
 */

#endif // SECRETS_H
