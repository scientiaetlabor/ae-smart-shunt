#include "ina226_adc.h"
#include <stdint.h>

INA226_ADC::INA226_ADC(uint8_t address, float shuntResistorOhms, float batteryCapacityAh)
    : ina226(address), ohms(shuntResistorOhms), batteryCapacity(batteryCapacityAh), lastUpdateTime(0),
      shuntVoltage_mV(-1), loadVoltage_V(-1), busVoltage_V(-1), current_mA(-1), power_mW(-1) {}

void INA226_ADC::begin(int sdaPin, int sclPin) {
    Wire.begin(sdaPin, sclPin);
    ina226.init();
    ina226.waitUntilConversionCompleted();

    ina226.setAverage(AVERAGE_16);
    ina226.setConversionTime(CONV_TIME_8244);
    ina226.setResistorRange(ohms, 100.0);
}

void INA226_ADC::readSensors() {
    ina226.readAndClearFlags();
    shuntVoltage_mV = ina226.getShuntVoltage_mV();
    busVoltage_V = ina226.getBusVoltage_V();
    current_mA = ina226.getCurrent_mA();
    power_mW = ina226.getBusPower();
    loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000);
}

float INA226_ADC::getShuntVoltage_mV() const {
    return shuntVoltage_mV;
}

float INA226_ADC::getBusVoltage_V() const {
    return busVoltage_V;
}

float INA226_ADC::getCurrent_mA() const {
    return current_mA;
}

float INA226_ADC::getPower_mW() const {
    return power_mW;
}

float INA226_ADC::getLoadVoltage_V() const {
    return loadVoltage_V;
}

float INA226_ADC::getBatteryCapacity() const {
    return batteryCapacity;
}

void INA226_ADC::updateBatteryCapacity(float currentA) {
    unsigned long currentTime = millis();

    if (lastUpdateTime == 0) {
        lastUpdateTime = currentTime;
        return;
    }

    float deltaTimeSec = (currentTime - lastUpdateTime) / 1000.0;
    float usedAh = (currentA * deltaTimeSec) / 3600.0;
    batteryCapacity -= usedAh;
    if (batteryCapacity < 0) batteryCapacity = 0;
    lastUpdateTime = currentTime;
}

bool INA226_ADC::isOverflow() const {
    return ina226.overflow;
}

String INA226_ADC::calculateRunFlatTimeFormatted(float runFlatHours, float warningThresholdHours, bool &warningTriggered) {
    warningTriggered = false;

    if (runFlatHours <= 0.0f) {
        return String("Infinite (no current draw)");
    }

    const float maxRunFlatHours = 24 * 365 * 10; // 10 years cap
    if (runFlatHours > maxRunFlatHours) {
        return String("> 10 years");
    }

    if (runFlatHours <= warningThresholdHours) {
        warningTriggered = true;
    }

    uint32_t totalMinutes = (uint32_t)(runFlatHours * 60);
    uint32_t weeks = totalMinutes / (7 * 24 * 60);
    totalMinutes %= (7 * 24 * 60);
    uint32_t days = totalMinutes / (24 * 60);
    totalMinutes %= (24 * 60);
    uint32_t hours = totalMinutes / 60;
    uint32_t minutes = totalMinutes % 60;

    String result;
    if (weeks > 0) {
        result += String(weeks) + (weeks == 1 ? " week " : " weeks ");
    }
    if (days > 0) {
        result += String(days) + (days == 1 ? " day " : " days ");
    }
    if (hours > 0) {
        result += String(hours) + (hours == 1 ? " hour " : " hours ");
    }
    if (minutes > 0 || result.length() == 0) {
        result += String(minutes) + (minutes == 1 ? " minute" : " minutes");
    }

    return result;
}

String INA226_ADC::getAveragedRunFlatTime(float currentA, float warningThresholdHours, bool &warningTriggered) {
    warningTriggered = false;

    const int minSamplesForAverage = 3;

    unsigned long now = millis();

    if (now - lastSampleTime < sampleIntervalSeconds * 1000UL) {
        if (sampleCount == 0) {
            return String("Gathering data...");
        } else if (sampleCount < minSamplesForAverage) {
            int lastSampleIndex = (sampleIndex + maxSamples - 1) % maxSamples;
            float lastSample = runFlatSamples[lastSampleIndex];
            warningTriggered = (lastSample <= warningThresholdHours);
            return calculateRunFlatTimeFormatted(lastSample, warningThresholdHours, warningTriggered);
        } else {
            float sum = 0;
            int validSamples = 0;
            for (int i = 0; i < sampleCount; i++) {
                if (runFlatSamples[i] > 0) {
                    sum += runFlatSamples[i];
                    validSamples++;
                }
            }
            float avgRunFlatHours = (validSamples > 0) ? (sum / validSamples) : -1.0f;
            warningTriggered = (avgRunFlatHours <= warningThresholdHours);
            return calculateRunFlatTimeFormatted(avgRunFlatHours, warningThresholdHours, warningTriggered);
        }
    }

    lastSampleTime = now;

    float currentRunFlatHours = (currentA > 0.001f) ? (batteryCapacity / currentA) : -1.0f;

    if (currentRunFlatHours >= 0) {
        runFlatSamples[sampleIndex] = currentRunFlatHours;
        sampleIndex = (sampleIndex + 1) % maxSamples;
        if (sampleCount < maxSamples) sampleCount++;
    }

    float sum = 0;
    int validSamples = 0;
    for (int i = 0; i < sampleCount; i++) {
        if (runFlatSamples[i] > 0) {
            sum += runFlatSamples[i];
            validSamples++;
        }
    }
    float avgRunFlatHours = (validSamples > 0) ? (sum / validSamples) : -1.0f;

    warningTriggered = (avgRunFlatHours >= 0) && (avgRunFlatHours <= warningThresholdHours);

    return calculateRunFlatTimeFormatted(avgRunFlatHours, warningThresholdHours, warningTriggered);
}