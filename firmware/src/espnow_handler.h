#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <esp_now.h>
#include <WiFi.h>
#include "shared_defs.h"

class ESPNowHandler {
public:
    ESPNowHandler(const uint8_t* broadcastAddress);

    bool begin();
    void registerSendCallback(esp_now_send_cb_t callback);
    bool addPeer();

    // Setter to copy data into internal struct
    void setAeSmartShuntStruct(const struct_message_ae_smart_shunt_1& shuntStruct);

    // Send the internal AeSmartShunt struct
    void sendMessageAeSmartShunt();

private:
    esp_now_peer_info_t peerInfo;
    uint8_t broadcastAddress[6];  // store a copy of the address

    struct_message_ae_smart_shunt_1 localAeSmartShuntStruct;  // internal copy of data
};

#endif // ESPNOW_HANDLER_H