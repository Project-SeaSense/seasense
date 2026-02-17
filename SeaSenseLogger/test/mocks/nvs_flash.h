#ifndef MOCK_NVS_FLASH_H
#define MOCK_NVS_FLASH_H

#include "esp_err.h"
#include "nvs.h"

inline esp_err_t nvs_flash_init() {
    return ESP_OK;
}

inline esp_err_t nvs_flash_erase() {
    mock_nvs_store().clear();
    return ESP_OK;
}

#endif
