#ifndef INA226_ADC_H
#define INA226_ADC_H

#include <INA226_WE.h>
#include <Wire.h>
#include <Arduino.h>

class INA226_ADC {
public:
    INA226_ADC(uint8_t address, float shuntResistorOhms, float batteryCapacityAh);
    void begin(int sdaPin, int sclPin);
    void readSensors();
    float getShuntVoltage_mV() const;
    float getBusVoltage_V() const;
    float getCurrent_mA() const;
    float getPower_mW() const;
    float getLoadVoltage_V() const;
    float getBatteryCapacity() const;
    void updateBatteryCapacity(float currentA); // current in A (positive = discharge)
    bool isOverflow() const;
    String getAveragedRunFlatTime(float currentA, float warningThresholdHours, bool &warningTriggered);

    // New method to calculate run-flat time and return formatted string
    String calculateRunFlatTimeFormatted(float currentA, float warningThresholdHours, bool &warningTriggered);

private:
    INA226_WE ina226;
    float ohms;

    float batteryCapacity;       // remaining capacity in Ah
    float maxBatteryCapacity;    // rated max capacity in Ah

    unsigned long lastUpdateTime;
    float shuntVoltage_mV;
    float loadVoltage_V;
    float busVoltage_V;
    float current_mA;
    float power_mW;

    // Averaging buffer for run-flat time (in hours)
    static const int maxSamples = 180; // e.g. 30 minutes at 10s intervals
    float runFlatSamples[maxSamples];
    int sampleIndex = 0;
    int sampleCount = 0;
    unsigned long lastSampleTime = 0;
    int sampleIntervalSeconds = 10; // sampling interval in seconds
};

#endif // INA226_ADC_H