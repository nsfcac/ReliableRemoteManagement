#pragma once
#include <Arduino.h>

class Heartbeat{
public:
    explicit Heartbeat(HardwareSerial& ser);

    void begin(int rxPin, int txPin, uint32_t baund);
    void tick();
    void send(uint32_t nowMs);

    bool peerAlive(uint32_t nowMs, uint32_t timeoutMs) const;
    uint32_t lastRxMS() const {return _lastRxMs;}
    char peerId() const {return _peerId;}

private:
    HardwareSerial& _ser;

    uint32_t _lastRxMs = 0;
    char _peerId = '?';

    uint8_t _state = 0;
    uint8_t _buf[2];
    uint8_t _idx = 0;

    void parseByte(uint8_t b);
    uint8_t crc8(const uint8_t* data, size_t n) const;
};