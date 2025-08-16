#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>
#include <string>
#include <iostream>
#include <vector>
#include <algorithm>

// Mock millis() function
unsigned long millis();
void set_mock_millis(unsigned long value);

void delay(unsigned long ms);

class MockSerial {
public:
    void begin(int speed) {}
    void print(const char* msg) { std::cout << msg; }
    void print(const std::string& msg) { std::cout << msg; }
    void print(int val) { std::cout << val; }
    void print(size_t val) { std::cout << val; }
    void print(float val) { std::cout << val; }
    void println(const char* msg) { std::cout << msg << std::endl; }
    void println(const std::string& msg) { std::cout << msg << std::endl; }
    void println(int val) { std::cout << val << std::endl; }
    void println(float val) { std::cout << val << std::endl; }
    void println() { std::cout << std::endl; }
    template<typename T>
    void printf(const char* format, T value) {
        // A very basic printf mock
        char buffer[256];
        snprintf(buffer, sizeof(buffer), format, value);
        std::cout << buffer;
    }
     template<typename T1, typename T2>
    void printf(const char* format, T1 val1, T2 val2) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), format, val1, val2);
        std::cout << buffer;
    }
};

extern MockSerial Serial;

// Basic String mock
class String : public std::string {
public:
    String(const char* c_str) : std::string(c_str) {}
    String(const std::string& s) : std::string(s) {}
    String() : std::string() {}
    String(unsigned int val) : std::string(std::to_string(val)) {}
    String(int val) : std::string(std::to_string(val)) {}
    String(long val) : std::string(std::to_string(val)) {}
    String(unsigned long val) : std::string(std::to_string(val)) {}
    String(float val) : std::string(std::to_string(val)) {}
    String(double val) : std::string(std::to_string(val)) {}

    void trim() {
        // basic trim mock
        size_t first = this->find_first_not_of(' ');
        if (std::string::npos == first) {
            this->clear();
            return;
        }
        size_t last = this->find_last_not_of(' ');
        *this = this->substr(first, (last - first + 1));
    }

    bool equalsIgnoreCase(const char* other) const {
        std::string s1(*this);
        std::string s2(other);
        std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
        std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
        return s1 == s2;
    }
};

#endif // ARDUINO_H
