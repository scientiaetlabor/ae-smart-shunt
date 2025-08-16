#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <string>
#include <map>

class Preferences {
public:
    void begin(const char* name, bool readOnly) {
        // In-memory mock doesn't need to do anything here
    }

    void end() {
        // In-memory mock doesn't need to do anything here
    }

    void putFloat(const char* key, float value) {
        preferences[key] = value;
    }

    float getFloat(const char* key, float defaultValue) {
        if (preferences.find(key) != preferences.end()) {
            return preferences[key];
        }
        return defaultValue;
    }

    void clear() {
        preferences.clear();
    }

    static void clear_static() {
        preferences.clear();
    }

private:
    static std::map<std::string, float> preferences;
};

#endif // PREFERENCES_H
