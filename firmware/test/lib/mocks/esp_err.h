#ifndef ESP_ERR_H
#define ESP_ERR_H

#define ESP_OK 0
#define ESP_FAIL -1

typedef int esp_err_t;

const char* esp_err_to_name(int err);

#endif // ESP_ERR_H
