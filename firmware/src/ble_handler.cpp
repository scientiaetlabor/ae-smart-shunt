#include "ble_handler.h"
#include <WiFi.h>
#include <esp_now.h>

BLEScan* pBLEScan;
BLEUtils utils;

BLEHandler::BLEHandler(struct_message_voltage0* voltageStruct) : localVoltage0Struct(voltageStruct) {
    memset(savedDeviceName, 0, sizeof(savedDeviceName));
}

void BLEHandler::startScan(int scanTimeSeconds) {
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(this);
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    pBLEScan->start(scanTimeSeconds, false);
}

void BLEHandler::stopScan() {
    if (pBLEScan) {
        pBLEScan->stop();
        pBLEScan->clearResults();
    }
}

char BLEHandler::convertCharToHex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 0;
}

void BLEHandler::prtnib(int n) {
    Serial.print((n & 8) ? "1" : "0");
    Serial.print((n & 4) ? "1" : "0");
    Serial.print((n & 2) ? "1" : "0");
    Serial.print((n & 1) ? "1" : "0");
}

void BLEHandler::onResult(BLEAdvertisedDevice* advertisedDevice) {
    String addr = advertisedDevice->getAddress().toString().c_str();

    if (addr.startsWith("ac:15:85")) {
        // Your existing sensor payload processing code here (omitted for brevity)
        // You can move the detailed payload parsing here if needed.
        return;
    }

    if (!advertisedDevice->haveManufacturerData()) {
        return;
    }

    std::string manData = advertisedDevice->getManufacturerData();
    int manDataSize = manData.length();

    if (manDataSize > 31) return; // safety check

    uint8_t manCharBuf[manDataSize + 1];
    memcpy(manCharBuf, manData.data(), manDataSize);
    manCharBuf[manDataSize] = 0;

    victronManufacturerData* vicData = (victronManufacturerData*)manCharBuf;

    if (vicData->vendorID != 0x02e1) return;
    if (vicData->victronRecordType != 0x09) {
        Serial.printf("Packet victronRecordType was 0x%x doesn't match 0x09\n", vicData->victronRecordType);
        return;
    }

    if (advertisedDevice->haveName()) {
        strncpy(savedDeviceName, advertisedDevice->getName().c_str(), sizeof(savedDeviceName) - 1);
    }

    if (vicData->encryptKeyMatch != key[0]) {
        Serial.printf("Packet encryption key byte 0x%2.2x doesn't match configured key[0] byte 0x%2.2x\n",
                      vicData->encryptKeyMatch, key[0]);
        return;
    }

    uint8_t inputData[16];
    uint8_t outputData[16] = {0};

    int encrDataSize = manDataSize - 10;
    for (int i = 0; i < encrDataSize; i++) {
        inputData[i] = vicData->victronEncryptedData[i];
    }

    esp_aes_context ctx;
    esp_aes_init(&ctx);

    auto status = esp_aes_setkey(&ctx, key, keyBits);
    if (status != 0) {
        Serial.printf("Error during esp_aes_setkey operation (%i).\n", status);
        esp_aes_free(&ctx);
        return;
    }

    uint8_t data_counter_lsb = (vicData->nonceDataCounter) & 0xff;
    uint8_t data_counter_msb = ((vicData->nonceDataCounter) >> 8) & 0xff;
    uint8_t nonce_counter[16] = {data_counter_lsb, data_counter_msb, 0};

    uint8_t stream_block[16] = {0};
    size_t nonce_offset = 0;

    status = esp_aes_crypt_ctr(&ctx, encrDataSize, &nonce_offset, nonce_counter, stream_block, inputData, outputData);
    if (status != 0) {
        Serial.printf("Error during esp_aes_crypt_ctr operation (%i).", status);
        esp_aes_free(&ctx);
        return;
    }
    esp_aes_free(&ctx);

    victronPanelData* victronData = (victronPanelData*)outputData;

    uint8_t deviceState = victronData->deviceState;
    uint8_t outputState = victronData->outputState;
    uint8_t errorCode = victronData->errorCode;
    uint16_t alarmReason = victronData->alarmReason;
    uint16_t warningReason = victronData->warningReason;
    float inputVoltage = float(victronData->inputVoltage) * 0.01;
    float outputVoltage = float(victronData->outputVoltage) * 0.01;
    uint32_t offReason = victronData->offReason;

    localVoltage0Struct->rearAuxBatt1V = outputVoltage;

    sendMessage();

    Serial.printf("%s, Battery: %.2f Volts, Load: %4.2f Volts, Alarm Reason: %d, Device State: %d, Error Code: %d, Warning Reason: %d, Off Reason: %d\n",
                  savedDeviceName,
                  inputVoltage, outputVoltage,
                  alarmReason, deviceState,
                  errorCode, warningReason,
                  offReason);
}

void BLEHandler::sendMessage() {
    esp_err_t result1 = esp_now_send(broadcastAddress, (uint8_t*)localVoltage0Struct, sizeof(*localVoltage0Struct));
    if (result1 == ESP_OK) {
        Serial.println("Sent message 3 with success");
    } else {
        Serial.println("Error sending the data");
    }
}