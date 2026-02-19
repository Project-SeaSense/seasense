#ifndef MOCK_ESP_TASK_WDT_H
#define MOCK_ESP_TASK_WDT_H

#include "esp_err.h"
#include <cstdint>

struct esp_task_wdt_config_t {
    uint32_t timeout_ms;
    int idle_core_mask;
    bool trigger_panic;
};

inline esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t*) { return ESP_OK; }
inline esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t*) { return ESP_OK; }
inline esp_err_t esp_task_wdt_add(void*) { return ESP_OK; }
inline esp_err_t esp_task_wdt_delete(void*) { return ESP_OK; }
inline esp_err_t esp_task_wdt_reset() { return ESP_OK; }

#endif
