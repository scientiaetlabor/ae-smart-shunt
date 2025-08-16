#ifndef ESP_NOW_H
#define ESP_NOW_H

#include <stdint.h>
#include <vector>

#define ESP_OK 0
#define ESP_FAIL -1

typedef enum {
    ESP_NOW_SEND_SUCCESS,
    ESP_NOW_SEND_FAIL
} esp_now_send_status_t;

typedef void (*esp_now_send_cb_t)(const uint8_t *mac_addr, esp_now_send_status_t status);

typedef struct esp_now_peer_info {
    uint8_t peer_addr[6];
    uint8_t channel;
    bool encrypt;
} esp_now_peer_info_t;

// Mock functions
int esp_now_init();
int esp_now_register_send_cb(esp_now_send_cb_t cb);
int esp_now_add_peer(const esp_now_peer_info_t *peer_info);
int esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len);

// Test helpers
void mock_esp_now_clear_sent_data();
const std::vector<uint8_t>& mock_esp_now_get_sent_data();


#endif // ESP_NOW_H
