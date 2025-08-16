#include "esp_now.h"

static std::vector<uint8_t> sent_data;

int esp_now_init() { return ESP_OK; }
int esp_now_register_send_cb(esp_now_send_cb_t cb) { return ESP_OK; }
int esp_now_add_peer(const esp_now_peer_info_t *peer_info) { return ESP_OK; }

int esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len) {
    sent_data.assign(data, data + len);
    return ESP_OK;
}

void mock_esp_now_clear_sent_data() {
    sent_data.clear();
}

const std::vector<uint8_t>& mock_esp_now_get_sent_data() {
    return sent_data;
}
