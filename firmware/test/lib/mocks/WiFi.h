#ifndef WIFI_H
#define WIFI_H

typedef enum {
    WIFI_MODE_NULL,
    WIFI_MODE_STA
} wifi_mode_t;

class MockWiFi {
public:
    void mode(wifi_mode_t mode) {}
};

extern MockWiFi WiFi;

#endif // WIFI_H
