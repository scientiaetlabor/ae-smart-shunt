#include <Arduino.h>
#include "shared_defs.h"
#include "ina226_adc.h"
#include "ble_handler.h"
#include "espnow_handler.h"
#include "passwords.h"
#include <esp_now.h>
#include <esp_err.h>


#define USE_ADC // if not defined, use victron BLE

float batteryCapacity = 100.0f; // Default rated battery capacity in Ah (used for SOC calc)

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct_message_ae_smart_shunt_1 ae_smart_shunt_struct;
INA226_ADC ina226_adc(I2C_ADDRESS, 0.001078f, 100.00f);
ESPNowHandler espNowHandler(broadcastAddress);

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setup()
{
  Serial.begin(115200);
  delay(100); // let Serial start

  ina226_adc.begin(6, 10);

  // Initialize ESP-NOW
  if (!espNowHandler.begin())
  {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Register callback for send status
  espNowHandler.registerSendCallback(onDataSent);
  // Ensure broadcast peer exists (some SDKs require an explicit broadcast peer)
  if (!espNowHandler.addPeer()) {
    Serial.println("Warning: failed to add broadcast peer; esp_now_send may return ESP_ERR_ESPNOW_NOT_FOUND on some platforms");
  } else {
    Serial.println("Broadcast peer added");
  }



#ifndef USE_ADC
  // Code to use victron BLE
  bleHandler.startScan(scanTime);
#endif

  Serial.println("Setup done");
}

void loop()
{
#ifdef USE_ADC
  ina226_adc.readSensors();

  // Populate struct fields
  ae_smart_shunt_struct.messageID = 1;
  ae_smart_shunt_struct.dataChanged = true;

  ae_smart_shunt_struct.batteryVoltage = ina226_adc.getBusVoltage_V();
  ae_smart_shunt_struct.batteryCurrent = ina226_adc.getCurrent_mA() / 1000.0f;
  ae_smart_shunt_struct.batteryPower   = ina226_adc.getPower_mW()   / 1000.0f;

  ae_smart_shunt_struct.batteryState = 0; // 0 = Normal, 1 = Warning, 2 = Critical

  // Update remaining capacity in the INA226 helper (assuming it expects current in A)
  ina226_adc.updateBatteryCapacity(ina226_adc.getCurrent_mA() / 1000.0f);

  // Get remaining Ah from INA helper
  float remainingAh = ina226_adc.getBatteryCapacity();
  ae_smart_shunt_struct.batteryCapacity = remainingAh; // remaining capacity in Ah
  // batteryCapacity global holds the rated capacity in Ah for SOC calculation
  if (batteryCapacity > 0.0f) {
    ae_smart_shunt_struct.batterySOC = remainingAh / batteryCapacity; // fraction 0..1
  } else {
    ae_smart_shunt_struct.batterySOC = 0.0f;
  }

  if (ina226_adc.isOverflow())
  {
    Serial.println("Overflow! Choose higher current range");
    ae_smart_shunt_struct.batteryState = 3; // overflow indicator
  }

  // Calculate and print run-flat time with warning threshold
  bool warning = false;
  float currentA = ina226_adc.getCurrent_mA() / 1000.0f; // convert mA to A
  float warningThresholdHours = 10.0f;

  String avgRunFlatTimeStr = ina226_adc.getAveragedRunFlatTime(currentA, warningThresholdHours, warning);

  strncpy(ae_smart_shunt_struct.runFlatTime, avgRunFlatTimeStr.c_str(), sizeof(ae_smart_shunt_struct.runFlatTime));
  ae_smart_shunt_struct.runFlatTime[sizeof(ae_smart_shunt_struct.runFlatTime) - 1] = '\0';  // ensure null termination

#else
  // Code to use victron BLE
  bleHandler.startScan(scanTime);

  // Provide safe defaults so struct is still valid
  ae_smart_shunt_struct.messageID = 0;
  ae_smart_shunt_struct.dataChanged = false;
  ae_smart_shunt_struct.batteryVoltage = 0.0f;
  ae_smart_shunt_struct.batteryCurrent = 0.0f;
  ae_smart_shunt_struct.batteryPower = 0.0f;
  ae_smart_shunt_struct.batteryCapacity = 0.0f;
  ae_smart_shunt_struct.batterySOC = 0.0f;
  ae_smart_shunt_struct.batteryState = 0;
  strncpy(ae_smart_shunt_struct.runFlatTime, "N/A", sizeof(ae_smart_shunt_struct.runFlatTime));
#endif

  // Send the data via ESP-NOW
  espNowHandler.setAeSmartShuntStruct(ae_smart_shunt_struct);
  espNowHandler.sendMessageAeSmartShunt();

#ifdef USE_ADC
  Serial.print("Average estimated run-flat time: ");
  Serial.println(String(ae_smart_shunt_struct.runFlatTime));
  if (ina226_adc.isOverflow())
  {
    Serial.println("Warning: Overflow condition!");
  }
#endif

  Serial.println();
  delay(10000);
}