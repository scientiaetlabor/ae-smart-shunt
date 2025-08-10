#include <Arduino.h>
#include "ina226_adc.h"
#include "espnow_handler.h"

ESPNowHandler::ESPNowHandler(const uint8_t *broadcastAddr)
{
    memcpy(broadcastAddress, broadcastAddr, 6);
    memset(&peerInfo, 0, sizeof(peerInfo));
}

void ESPNowHandler::setAeSmartShuntStruct(const struct_message_ae_smart_shunt_1 &shuntStruct)
{
    localAeSmartShuntStruct.messageID = shuntStruct.messageID;
    localAeSmartShuntStruct.dataChanged = shuntStruct.dataChanged;
    localAeSmartShuntStruct.batteryVoltage = shuntStruct.batteryVoltage;
    localAeSmartShuntStruct.batteryCurrent = shuntStruct.batteryCurrent;
    localAeSmartShuntStruct.batteryPower = shuntStruct.batteryPower;
    localAeSmartShuntStruct.batterySOC = shuntStruct.batterySOC;
    localAeSmartShuntStruct.batteryCapacity = shuntStruct.batteryCapacity;
    localAeSmartShuntStruct.batteryState = shuntStruct.batteryState;

    strncpy(localAeSmartShuntStruct.runFlatTime, shuntStruct.runFlatTime, sizeof(localAeSmartShuntStruct.runFlatTime));
    localAeSmartShuntStruct.runFlatTime[sizeof(localAeSmartShuntStruct.runFlatTime) - 1] = '\0';
}

void printMacAddress(const uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        if (mac[i] < 16) Serial.print("0");  // leading zero for single hex digit
        Serial.print(mac[i], HEX);
    }
    Serial.println();
}

void ESPNowHandler::sendMessageAeSmartShunt()
{
    uint8_t *data = (uint8_t *)&localAeSmartShuntStruct;
    size_t len = sizeof(localAeSmartShuntStruct);

    Serial.print("Sending data: ");
    for (size_t i = 0; i < len; i++)
    {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();

    esp_err_t result = esp_now_send(broadcastAddress, data, len);
    if (result == ESP_OK)
    {
        Serial.println("Sent AeSmartShunt message successfully");
    }
    else
    {
        Serial.print("Broadcast Address: ");
        printMacAddress(broadcastAddress);
        Serial.printf("Error sending AeSmartShunt data to: 0x%04X\n", result);
    }
}

bool ESPNowHandler::begin()
{
    WiFi.mode(WIFI_MODE_STA);
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return false;
    }
    return true;
}

void ESPNowHandler::registerSendCallback(esp_now_send_cb_t callback)
{
    esp_now_register_send_cb(callback);
}

bool ESPNowHandler::addPeer()
{
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer");
        return false;
    }
    return true;
}
