#include <Arduino.h>
#include "shared_defs.h"
#include "ina226_adc.h"
#include "ble_handler.h"
#include "espnow_handler.h"
#include "passwords.h"

#define USE_ADC // if not defined, use victron BLE

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct_message_voltage0 localVoltage0Struct;
INA226_ADC ina226_adc(I2C_ADDRESS, 0.001078, 100.00);
BLEHandler bleHandler(&localVoltage0Struct);
ESPNowHandler espNowHandler(broadcastAddress);

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup() {
  Serial.begin(115200);

  ina226_adc.begin(6, 10);

  if (!espNowHandler.begin()) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  espNowHandler.registerSendCallback(OnDataSent);
  if (!espNowHandler.addPeer()) {
    Serial.println("Failed to add ESP-NOW peer");
    return;
  }

  bleHandler.startScan(scanTime);

  Serial.println("Setup done");
}

void loop() {
  #ifdef USE_ADC
    ina226_adc.readSensors();
    
    Serial.print("Battery Voltage [V]: "); Serial.println(ina226_adc.getBusVoltage_V());
    Serial.print("Current [A]: "); Serial.println(ina226_adc.getCurrent_mA()/1000);
    Serial.print("Bus Power [W]: "); Serial.println(ina226_adc.getPower_mW()/1000);

    localVoltage0Struct.rearAuxBatt1V = ina226_adc.getBusVoltage_V();
    localVoltage0Struct.rearAuxBatt1I = ina226_adc.getCurrent_mA()/1000;

    ina226_adc.updateBatteryCapacity(ina226_adc.getCurrent_mA()/1000);

    Serial.print("Remaining Capacity [Ah]: ");
    Serial.println(ina226_adc.getBatteryCapacity(), 2);

    if(!ina226_adc.isOverflow()){
      Serial.println("Values OK - no overflow");
    }
    else{
      Serial.println("Overflow! Choose higher current range");
    }

    // Calculate and print run-flat time with warning threshold
    bool warning = false;
    float currentA = ina226_adc.getCurrent_mA() / 1000.0f; // convert mA to A
    float warningThresholdHours = 2.0f;

  String avgRunFlatTimeStr = ina226_adc.getAveragedRunFlatTime(currentA, warningThresholdHours, warning);

  Serial.print("Average estimated run-flat time: ");
  Serial.println(avgRunFlatTimeStr);

  if (warning) {
      Serial.println("Warning: Average run-flat time below threshold!");
  }
  #endif

  Serial.println();
  delay(10000);

  bleHandler.startScan(scanTime);
}