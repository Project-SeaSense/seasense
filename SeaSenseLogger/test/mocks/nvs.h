#ifndef MOCK_NVS_H
#define MOCK_NVS_H

#include "esp_err.h"
#include <cstdint>
#include <map>
#include <string>

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READWRITE = 1 } nvs_open_mode_t;

// Backing store for NVS mock — accessible from tests for verification
inline std::map<std::string, uint32_t>& mock_nvs_store() {
    static std::map<std::string, uint32_t> store;
    return store;
}

inline esp_err_t nvs_open(const char* ns, nvs_open_mode_t, nvs_handle_t* handle) {
    *handle = 1;
    return ESP_OK;
}

inline esp_err_t nvs_get_u32(nvs_handle_t, const char* key, uint32_t* out) {
    auto& store = mock_nvs_store();
    auto it = store.find(key);
    if (it == store.end()) return ESP_ERR_NVS_NOT_FOUND;
    *out = it->second;
    return ESP_OK;
}

inline esp_err_t nvs_set_u32(nvs_handle_t, const char* key, uint32_t value) {
    mock_nvs_store()[key] = value;
    return ESP_OK;
}

inline esp_err_t nvs_erase_all(nvs_handle_t) { mock_nvs_store().clear(); return ESP_OK; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
inline void nvs_close(nvs_handle_t) {}

#endif
