#ifndef ESPNOW_HANDLER_H
#define ESPNOW_HANDLER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "shared_defs.h" // defines struct_message_ae_smart_shunt_1

class ESPNowHandler {
public:
    ESPNowHandler(const uint8_t *broadcastAddr);

    bool begin();
    void registerSendCallback(esp_now_send_cb_t callback);
    bool addPeer();

    // Copy the struct into the handler for later sending
    void setAeSmartShuntStruct(const struct_message_ae_smart_shunt_1 &shuntStruct);

    // Send the currently stored struct using ESP-NOW (broadcast addr by default)
    void sendMessageAeSmartShunt();

private:
    uint8_t broadcastAddress[6];
    esp_now_peer_info_t peerInfo;
    struct_message_ae_smart_shunt_1 localAeSmartShuntStruct;

    void printMacAddress(const uint8_t* mac);
};

#endif // ESPNOW_HANDLER_H