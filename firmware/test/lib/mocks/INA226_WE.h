#ifndef INA226_WE_H
#define INA226_WE_H

#include <Arduino.h>

// Define enums that are used in ina226_adc.cpp
enum ina226_averages{
    AVERAGE_1,
    AVERAGE_4,
    AVERAGE_16,
    AVERAGE_64,
    AVERAGE_128,
    AVERAGE_256,
    AVERAGE_512,
    AVERAGE_1024
};

enum ina226_conversion_times{
    CONV_TIME_140,
    CONV_TIME_204,
    CONV_TIME_332,
    CONV_TIME_588,
    CONV_TIME_1100,
    CONV_TIME_2116,
    CONV_TIME_4156,
    CONV_TIME_8244
};


class INA226_WE {
public:
    INA226_WE(uint8_t addr) {
        // Mock constructor
    }

    void init() {}
    void waitUntilConversionCompleted() {}
    void setAverage(ina226_averages averages) {}
    void setConversionTime(ina226_conversion_times convTime) {}
    void setResistorRange(float resistor, float current) {}
    void readAndClearFlags() {}

    // Mock data members - public to allow easy manipulation in tests
    static float mockShuntVoltage_mV;
    static float mockBusVoltage_V;
    static float mockCurrent_mA;
    static float mockBusPower;
    static bool overflow;

    // Mock methods to return the mock data
    float getShuntVoltage_mV() { return mockShuntVoltage_mV; }
    float getBusVoltage_V() { return mockBusVoltage_V; }
    float getCurrent_mA() { return mockCurrent_mA; }
    float getBusPower() { return mockBusPower; }
};

#endif // INA226_WE_H
