#ifndef MOCK_ESP_SYSTEM_H
#define MOCK_ESP_SYSTEM_H

#include "esp_err.h"

typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON,
    ESP_RST_EXT,
    ESP_RST_SW,
    ESP_RST_PANIC,
    ESP_RST_INT_WDT,
    ESP_RST_TASK_WDT,
    ESP_RST_WDT,
    ESP_RST_DEEPSLEEP,
    ESP_RST_BROWNOUT,
    ESP_RST_SDIO
} esp_reset_reason_t;

// Injectable mock — tests set this before calling begin()
inline esp_reset_reason_t _mock_reset_reason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return _mock_reset_reason; }

#endif
