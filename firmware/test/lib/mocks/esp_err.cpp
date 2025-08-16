#include "esp_err.h"

const char* esp_err_to_name(int err) {
    if (err == ESP_OK) return "ESP_OK";
    if (err == ESP_FAIL) return "ESP_FAIL";
    return "UNKNOWN";
}
