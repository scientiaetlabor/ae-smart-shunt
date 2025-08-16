#ifndef WIRE_H
#define WIRE_H

class MockWire {
public:
    void begin(int sda, int scl) {}
};

extern MockWire Wire;

#endif // WIRE_H
