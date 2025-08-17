#ifndef INA226_ADC_H
#define INA226_ADC_H

#include <INA226_WE.h>
#include <Wire.h>
#include <Arduino.h>
#include <Preferences.h>

class INA226_ADC {
public:
    INA226_ADC(uint8_t address, float shuntResistorOhms, float batteryCapacityAh);
    void begin(int sdaPin, int sclPin);
    void readSensors();
    float getShuntVoltage_mV() const;
    float getBusVoltage_V() const;
    float getCurrent_mA() const;      // calibrated current (mA)
    float getRawCurrent_mA() const;   // raw measured current (mA) from INA226
    float getPower_mW() const;
    float getLoadVoltage_V() const;
    float getBatteryCapacity() const;
    void setBatteryCapacity(float capacity);
    void updateBatteryCapacity(float currentA); // current in A (positive = discharge)
    bool isOverflow() const;
    String getAveragedRunFlatTime(float currentA, float warningThresholdHours, bool &warningTriggered);

    // Calibration APIs
    // Try to load persisted calibration for the given shunt rating.
    // Returns true if a persisted calibration was found and applied; false otherwise.
    bool loadCalibration(uint16_t shuntRatedA);
    // Persist calibration for a shunt rating
    bool saveCalibration(uint16_t shuntRatedA, float gain, float offset_mA);
    // Set/get in-memory calibration
    void setCalibration(float gain, float offset_mA);
    void getCalibration(float &gainOut, float &offsetOut) const;
    // Query stored calibration without applying it; returns true if entry exists.
    bool getStoredCalibrationForShunt(uint16_t shuntRatedA, float &gainOut, float &offsetOut) const;

    // Format run-flat time from current and return string
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
    float current_mA;            // raw measured mA from last readSensors()
    float power_mW;

    // Calibration params (applied to raw mA to get calibrated mA)
    float calibrationGain;       // multiplier
    float calibrationOffset_mA;  // additive offset in mA

    // Averaging buffer for run-flat time (in hours)
    static const int maxSamples = 180; // e.g. 30 minutes at 10s intervals
    float runFlatSamples[maxSamples];
    int sampleIndex = 0;
    int sampleCount = 0;
    unsigned long lastSampleTime = 0;
    int sampleIntervalSeconds = 10; // sampling interval in seconds
};

#endif // INA226_ADC_H