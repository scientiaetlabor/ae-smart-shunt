#include "ina226_adc.h"

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
      sampleIndex(0),
      sampleCount(0),
      lastSampleTime(0),
      sampleIntervalSeconds(10)
{
    // initialize sample buffer to invalid marker (-1)
    for (int i = 0; i < maxSamples; ++i) runFlatSamples[i] = -1.0f;
}

void INA226_ADC::begin(int sdaPin, int sclPin) {
    Wire.begin(sdaPin, sclPin);
    ina226.init();
    ina226.waitUntilConversionCompleted();

    // Configure averaging and conversion times (depending on INA226_WE library defines)
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
    loadVoltage_V = busVoltage_V + (shuntVoltage_mV / 1000.0f);
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

    float deltaTimeSec = (currentTime - lastUpdateTime) / 1000.0f;

    // Compute delta in Ah over the elapsed time
    // deltaAh positive when currentA is positive (discharge)
    float deltaAh = (currentA * deltaTimeSec) / 3600.0f;

    // Positive current = discharge (removes capacity). Negative current = charge (adds capacity).
    batteryCapacity -= deltaAh;

    // Cap remaining capacity between 0 and the rated max
    if (batteryCapacity < 0.0f) batteryCapacity = 0.0f;
    if (batteryCapacity > maxBatteryCapacity) batteryCapacity = maxBatteryCapacity;

    lastUpdateTime = currentTime;
}

bool INA226_ADC::isOverflow() const {
    // INA226_WE exposes overflow as a public member 'overflow'
    return ina226.overflow;
}

String INA226_ADC::calculateRunFlatTimeFormatted(float currentA, float warningThresholdHours, bool &warningTriggered) {
    warningTriggered = false;

    // Treat very small currents as no draw
    if (currentA > -0.001f && currentA < 0.001f) {
        return String("Infinite (no current draw)");
    }

    const float maxRunFlatHours = 24.0f * 7.0f; // 7 day cap
    float runHours = -1.0f;
    bool charging = false;

    if (currentA > 0.001f) {
        // Discharging: hours until flat
        runHours = batteryCapacity / currentA;
        charging = false;
    } else if (currentA < -0.001f) {
        // Charging: hours until full
        float remainingToFullAh = maxBatteryCapacity - batteryCapacity;
        if (remainingToFullAh <= 0.0f) {
            return String("Fully Charged!");
        }
        runHours = remainingToFullAh / (-currentA); // positive hours
        charging = true;
    }

    if (runHours <= 0.0f) {
        // If something odd happened, treat as no draw
        return String("Infinite (no current draw)");
    }

    if (runHours > maxRunFlatHours) {
        return String("> 7 days");
    }

    // For both charging and discharging, trigger warning if within threshold
    if (runHours <= warningThresholdHours) {
        warningTriggered = true;
    }

    uint32_t totalMinutes = (uint32_t)(runHours * 60.0f);
    totalMinutes %= (7 * 24 * 60);
    uint32_t days = totalMinutes / (24 * 60);
    totalMinutes %= (24 * 60);
    uint32_t hours = totalMinutes / 60;
    uint32_t minutes = totalMinutes % 60;

    String result;
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

    // If not enough time has passed since last sample, return based on gathered samples
    if (now - lastSampleTime < (unsigned long)sampleIntervalSeconds * 1000UL) {
        if (sampleCount == 0) {
            return String("Gathering data...");
        } else if (sampleCount < minSamplesForAverage) {
            // Use the last valid sample
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
            // convert averaged run-flat hours back to a current approximation for formatting convenience:
            // If avgRunFlatHours > 0, approximate currentA_for_format = batteryCapacity / avgRunFlatHours
            float approxCurrentA = (avgRunFlatHours > 0.0f) ? (batteryCapacity / avgRunFlatHours) : 0.0f;
            return calculateRunFlatTimeFormatted(approxCurrentA, warningThresholdHours, warningTriggered);
        }
    }

    // Time to sample
    lastSampleTime = now;

    // compute current run-flat hours for this sample
    float currentRunFlatHours = (currentA > 0.001f) ? (batteryCapacity / currentA) : -1.0f;

    if (currentRunFlatHours >= 0.0f) {
        runFlatSamples[sampleIndex] = currentRunFlatHours;
        sampleIndex = (sampleIndex + 1) % maxSamples;
        if (sampleCount < maxSamples) sampleCount++;
    }

    // compute average of valid samples
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

    // convert avgRunFlatHours back to an approximate current for formatting (same approach as above)
    float approxCurrentA = (avgRunFlatHours > 0.0f) ? (batteryCapacity / avgRunFlatHours) : 0.0f;
    return calculateRunFlatTimeFormatted(approxCurrentA, warningThresholdHours, warningTriggered);
}