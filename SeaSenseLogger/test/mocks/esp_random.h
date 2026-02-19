#ifndef MOCK_ESP_RANDOM_H
#define MOCK_ESP_RANDOM_H

#include <cstdint>
#include <cstdlib>

inline uint32_t esp_random() {
    return (uint32_t)rand();
}

#endif
