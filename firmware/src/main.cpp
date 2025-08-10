#include <Arduino.h>
#include "shared_defs.h"
#include "ina226_adc.h"
#include "ble_handler.h"
#include "espnow_handler.h"
#include "passwords.h"

#define USE_ADC // if not defined, use victron BLE

float batteryCapacity = 100.0; // Default battery capacity in Ah

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct_message_ae_smart_shunt_1 ae_smart_shunt_struct;
INA226_ADC ina226_adc(I2C_ADDRESS, 0.001078, 100.00);
ESPNowHandler espNowHandler(broadcastAddress);

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setup()
{
  Serial.begin(115200);

  ina226_adc.begin(6, 10);

  // Set the voltage struct pointer in the handler
  espNowHandler.setAeSmartShuntStruct(ae_smart_shunt_struct);

  // Initialize ESP-NOW
  if (!espNowHandler.begin())
  {
    Serial.println("ESP-NOW init failed");
    return;
  }

  // Add peer // not needed for broadcast
  //if (!espNowHandler.addPeer())
  //{
  //  Serial.println("Failed to add peer");
  //  return;
  //}

  espNowHandler.registerSendCallback(onDataSent);

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

  ae_smart_shunt_struct.messageID = 1;
  ae_smart_shunt_struct.dataChanged = true;

  //Serial.print("Battery Voltage [V]: ");
  //Serial.println(ina226_adc.getBusVoltage_V());
  ae_smart_shunt_struct.batteryVoltage = ina226_adc.getBusVoltage_V();
  //Serial.print("Current [A]: ");
  //Serial.println(ina226_adc.getCurrent_mA() / 1000);
  ae_smart_shunt_struct.batteryCurrent = ina226_adc.getCurrent_mA() / 1000;
  //Serial.print("Bus Power [W]: ");
  //Serial.println(ina226_adc.getPower_mW() / 1000);
  ae_smart_shunt_struct.batteryCurrent = ina226_adc.getPower_mW() / 1000;

  ae_smart_shunt_struct.batteryState = 0; // 0 = Normal, 1 = Warning, 2 = Critical

  ina226_adc.updateBatteryCapacity(ina226_adc.getCurrent_mA() / 1000); // Update battery capacity based on current draw

  //Serial.print("Remaining Capacity [Ah]: ");
  //Serial.println(ina226_adc.getBatteryCapacity(), 2);

  ae_smart_shunt_struct.batteryCapacity = ina226_adc.getBatteryCapacity(), 2;

  if (ina226_adc.isOverflow())
  {
    Serial.println("Overflow! Choose higher current range");
    ae_smart_shunt_struct.batteryState = 3; // 0 = Normal, 1 = Warning, 2 = Critical
  }

  ae_smart_shunt_struct.batterySOC = ina226_adc.getBatteryCapacity() / batteryCapacity; // Assuming 100Ah is the full capacity for simplicity
  ae_smart_shunt_struct.batteryCapacity = batteryCapacity;

  // Calculate and print run-flat time with warning threshold
  bool warning = false;
  float currentA = ina226_adc.getCurrent_mA() / 1000.0f; // convert mA to A
  float warningThresholdHours = 10.0f;
#else
  // Code to use victron BLE
  bleHandler.startScan(scanTime);
#endif

  String avgRunFlatTimeStr = ina226_adc.getAveragedRunFlatTime(currentA, warningThresholdHours, warning);

  strncpy(ae_smart_shunt_struct.runFlatTime, avgRunFlatTimeStr.c_str(), sizeof(ae_smart_shunt_struct.runFlatTime));
  ae_smart_shunt_struct.runFlatTime[sizeof(ae_smart_shunt_struct.runFlatTime) - 1] = '\0';  // ensure null termination

  // Send the data via ESP-NOW
  espNowHandler.setAeSmartShuntStruct(ae_smart_shunt_struct);

  espNowHandler.sendMessageAeSmartShunt();

  Serial.print("Average estimated run-flat time: ");
  Serial.println(avgRunFlatTimeStr);

  if (warning)
  {
    Serial.println("Warning: Average run-flat time below threshold!");
  }

  Serial.println();
  delay(10000);
}