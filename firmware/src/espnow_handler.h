#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <esp_now.h>
#include <WiFi.h>

class ESPNowHandler {
public:
    ESPNowHandler(const uint8_t* broadcastAddress);
    bool begin();
    void registerSendCallback(esp_now_send_cb_t callback);
    bool addPeer();
    esp_err_t sendData(const uint8_t* data, size_t len);

private:
    esp_now_peer_info_t peerInfo;
    const uint8_t* broadcastAddress;
};

#endif // ESPNOW_HANDLER_H