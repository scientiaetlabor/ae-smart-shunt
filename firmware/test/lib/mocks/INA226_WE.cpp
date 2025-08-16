#include "INA226_WE.h"

float INA226_WE::mockShuntVoltage_mV = 0.0;
float INA226_WE::mockBusVoltage_V = 0.0;
float INA226_WE::mockCurrent_mA = 0.0;
float INA226_WE::mockBusPower = 0.0;
bool INA226_WE::overflow = false;
