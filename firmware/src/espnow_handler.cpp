#include <Arduino.h>
#include "ina226_adc.h"
#include "espnow_handler.h"
#include "shared_defs.h"

ESPNowHandler::ESPNowHandler(const uint8_t* broadcastAddress) : broadcastAddress(broadcastAddress) {
    memset(&peerInfo, 0, sizeof(peerInfo));
}

bool ESPNowHandler::begin() {
    WiFi.mode(WIFI_MODE_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return false;
    }
    return true;
}

void ESPNowHandler::registerSendCallback(esp_now_send_cb_t callback) {
    esp_now_register_send_cb(callback);
}

bool ESPNowHandler::addPeer() {
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return false;
    }
    return true;
}

esp_err_t ESPNowHandler::sendData(const uint8_t* data, size_t len) {
    return esp_now_send(broadcastAddress, data, len);
}