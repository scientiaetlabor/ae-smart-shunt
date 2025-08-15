#include "ina226_adc.h"
#include <cfloat>

INA226_ADC::INA226_ADC(uint8_t address, float shuntResistorOhms, float batteryCapacityAh)
    : ina226(address),
      ohms(shuntResistorOhms),
      batteryCapacity(batteryCapacityAh),
      maxBatteryCapacity(batteryCapacityAh),
      lastUpdateTime(0),
      shuntVoltage_mV(-1),
      loadVoltage_V(-1),
      busVoltage_V(-1),
      current_mA(-1),
      power_mW(-1),
      calibrationGain(1.0f),
      calibrationOffset_mA(0.0f),
      sampleIndex(0),
      sampleCount(0),
      lastSampleTime(0),
      sampleIntervalSeconds(10)
{
    for (int i = 0; i < maxSamples; ++i) runFlatSamples[i] = -1.0f;
}

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
    current_mA = ina226.getCurrent_mA(); // raw mA
    power_mW = ina226.getBusPower();
    loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000.0f);
}

float INA226_ADC::getShuntVoltage_mV() const {
    return shuntVoltage_mV;
}

float INA226_ADC::getBusVoltage_V() const {
    return busVoltage_V;
}

float INA226_ADC::getRawCurrent_mA() const {
    return current_mA;
}

float INA226_ADC::getCurrent_mA() const {
    // Apply linear calibration to raw reading
    return (current_mA * calibrationGain) + calibrationOffset_mA;
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

void INA226_ADC::setCalibration(float gain, float offset_mA) {
    calibrationGain = gain;
    calibrationOffset_mA = offset_mA;
}

void INA226_ADC::getCalibration(float &gainOut, float &offsetOut) const {
    gainOut = calibrationGain;
    offsetOut = calibrationOffset_mA;
}

// Try to load a persisted calibration for a given shunt rating and apply it.
// Returns true if a persisted calibration existed and was applied; false otherwise.
bool INA226_ADC::loadCalibration(uint16_t shuntRatedA) {
    Preferences prefs;
    prefs.begin("ina_cal", true);
    char keyGain[16];
    char keyOff[16];
    snprintf(keyGain, sizeof(keyGain), "g_%u", (unsigned)shuntRatedA);
    snprintf(keyOff, sizeof(keyOff), "o_%u", (unsigned)shuntRatedA);

    const float sentinel = 1e30f;
    float g = prefs.getFloat(keyGain, sentinel);
    float o = prefs.getFloat(keyOff, sentinel);
    prefs.end();

    if (g == sentinel && o == sentinel) {
        // No stored calibration for this shunt rating
        return false;
    }

    // If one was present, apply values (if one missing, replace with sensible defaults)
    if (g == sentinel) g = 1.0f;
    if (o == sentinel) o = 0.0f;

    calibrationGain = g;
    calibrationOffset_mA = o;
    return true;
}

// Query whether a stored calibration exists for the given shunt rating.
// Does not modify the active calibration in memory.
bool INA226_ADC::getStoredCalibrationForShunt(uint16_t shuntRatedA, float &gainOut, float &offsetOut) const {
    Preferences prefs;
    prefs.begin("ina_cal", true);
    char keyGain[16];
    char keyOff[16];
    snprintf(keyGain, sizeof(keyGain), "g_%u", (unsigned)shuntRatedA);
    snprintf(keyOff, sizeof(keyOff), "o_%u", (unsigned)shuntRatedA);

    const float sentinel = 1e30f;
    float g = prefs.getFloat(keyGain, sentinel);
    float o = prefs.getFloat(keyOff, sentinel);
    prefs.end();

    if (g == sentinel && o == sentinel) {
        return false;
    }

    // If one missing use defaults for that entry
    if (g == sentinel) g = 1.0f;
    if (o == sentinel) o = 0.0f;

    gainOut = g;
    offsetOut = o;
    return true;
}

bool INA226_ADC::saveCalibration(uint16_t shuntRatedA, float gain, float offset_mA) {
    Preferences prefs;
    prefs.begin("ina_cal", false);
    char keyGain[16];
    char keyOff[16];
    snprintf(keyGain, sizeof(keyGain), "g_%u", (unsigned)shuntRatedA);
    snprintf(keyOff, sizeof(keyOff), "o_%u", (unsigned)shuntRatedA);
    prefs.putFloat(keyGain, gain);
    prefs.putFloat(keyOff, offset_mA);
    prefs.end();

    calibrationGain = gain;
    calibrationOffset_mA = offset_mA;
    return true;
}

void INA226_ADC::updateBatteryCapacity(float currentA) {
    unsigned long currentTime = millis();

    if (lastUpdateTime == 0) {
        lastUpdateTime = currentTime;
        return;
    }

    float deltaTimeSec = (currentTime - lastUpdateTime) / 1000.0f;
    float deltaAh = (currentA * deltaTimeSec) / 3600.0f;
    batteryCapacity -= deltaAh;
    if (batteryCapacity < 0.0f) batteryCapacity = 0.0f;
    if (batteryCapacity > maxBatteryCapacity) batteryCapacity = maxBatteryCapacity;
    lastUpdateTime = currentTime;
}

bool INA226_ADC::isOverflow() const {
    return ina226.overflow;
}

String INA226_ADC::calculateRunFlatTimeFormatted(float currentA, float warningThresholdHours, bool &warningTriggered) {
    warningTriggered = false;

    const float maxRunFlatHours = 24.0f * 7.0f;
    float runHours = -1.0f;
    bool charging = false;
    
    // Define a small tolerance for "fully charged" state, e.g., 99.5%
    const float fullyChargedThreshold = maxBatteryCapacity * 0.995f;

    if (currentA > 0.001f) {
        runHours = batteryCapacity / currentA;
        charging = false;
    } else if (currentA < -0.001f) {
        if (batteryCapacity >= fullyChargedThreshold) {
             return String("Fully Charged!");
        }
        float remainingToFullAh = maxBatteryCapacity - batteryCapacity;
        runHours = remainingToFullAh / (-currentA);
        charging = true;
    }

    if (runHours <= 0.0f) {
      return String("Calculating...");
    }

    if (runHours > maxRunFlatHours) {
        return String("> 7 days");
    }

    if (runHours <= warningThresholdHours) {
        warningTriggered = true;
    }

    uint32_t totalMinutes = (uint32_t)(runHours * 60.0f);
    totalMinutes %= (7 * 24 * 60);
    uint32_t days = totalMinutes / (24 * 60);
    totalMinutes %= (24 * 60);
    uint32_t hours = totalMinutes / 60;

    String result;
    if (days > 0) {
        result += String(days) + (days == 1 ? " day " : " days ");         
    }
    if (hours > 0) {
        result += String(hours) + (hours == 1 ? " hour " : " hours ");
    }
    
    // Add "until flat" or "until full" based on charging state
    if (!charging) {
        result += "until flat";
    } else {
        result += "until full";
    }

    return result;
}

String INA226_ADC::getAveragedRunFlatTime(float currentA, float warningThresholdHours, bool &warningTriggered) {
    warningTriggered = false;
    const int minSamplesForAverage = 3;
    unsigned long now = millis();

    if (now - lastSampleTime < (unsigned long)sampleIntervalSeconds * 1000UL) {
        if (sampleCount == 0) {
            return String("Gathering data...");
        } else if (sampleCount < minSamplesForAverage) {
            int lastSampleIndex = (sampleIndex + maxSamples - 1) % maxSamples;
            float lastSample = runFlatSamples[lastSampleIndex];
            if (lastSample <= 0.0f) return String("Gathering data...");
            warningTriggered = (lastSample <= warningThresholdHours);
            return calculateRunFlatTimeFormatted((lastSample > 0.0f) ? (batteryCapacity / lastSample) : 0.0f, warningThresholdHours, warningTriggered);
        } else {
            float sum = 0.0f;
            int validSamples = 0;
            for (int i = 0; i < sampleCount; i++) {
                if (runFlatSamples[i] > 0.0f) {
                    sum += runFlatSamples[i];
                    validSamples++;
                }
            }
            float avgRunFlatHours = (validSamples > 0) ? (sum / validSamples) : -1.0f;
            warningTriggered = (avgRunFlatHours >= 0.0f) && (avgRunFlatHours <= warningThresholdHours);
            float approxCurrentA = (avgRunFlatHours > 0.0f) ? (batteryCapacity / avgRunFlatHours) : 0.0f;
            return calculateRunFlatTimeFormatted(approxCurrentA, warningThresholdHours, warningTriggered);
        }
    }

    lastSampleTime = now;

    float currentRunFlatHours = (currentA > 0.001f) ? (batteryCapacity / currentA) : -1.0f;

    if (currentRunFlatHours >= 0.0f) {
        runFlatSamples[sampleIndex] = currentRunFlatHours;
        sampleIndex = (sampleIndex + 1) % maxSamples;
        if (sampleCount < maxSamples) sampleCount++;
    }

    float sum = 0.0f;
    int validSamples = 0;
    for (int i = 0; i < sampleCount; i++) {
        if (runFlatSamples[i] > 0.0f) {
            sum += runFlatSamples[i];
            validSamples++;
        }
    }
    float avgRunFlatHours = (validSamples > 0) ? (sum / validSamples) : -1.0f;

    warningTriggered = (avgRunFlatHours >= 0.0f) && (avgRunFlatHours <= warningThresholdHours);
    float approxCurrentA = (avgRunFlatHours > 0.0f) ? (batteryCapacity / avgRunFlatHours) : 0.0f;
    return calculateRunFlatTimeFormatted(approxCurrentA, warningThresholdHours, warningTriggered);
}