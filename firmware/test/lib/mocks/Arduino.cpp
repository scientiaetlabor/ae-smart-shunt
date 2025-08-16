#include "Arduino.h"

static unsigned long mock_millis_value = 0;

unsigned long millis() {
    return mock_millis_value;
}

void set_mock_millis(unsigned long value) {
    mock_millis_value = value;
}

void delay(unsigned long ms) {
    mock_millis_value += ms;
}

MockSerial Serial;
