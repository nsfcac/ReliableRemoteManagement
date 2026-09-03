#include "Heartbeat.h"

Heartbeat::Heartbeat(HardwareSerial& ser) : _ser(ser) {}

void Heartbeat::begin(int rxPin, int txPin, uint32_t baud){
    _ser.begin(baud, SERIAL_8N1, rxPin, txPin);
}

uint8_t Heartbeat::crc8(const uint8_t* data, size_t n) const {
    uint8_t crc = 0;
    for(size_t i = 0; i<n; i++){
        crc ^= data[i];
        for(int b=0; b<8; b++){
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void Heartbeat::send(uint32_t /*nowMs*/){
    static uint8_t seq = 0;

    // Frame: AA 55 SEQ CRC, where CRC covers SEQ only. No device ID: the
    // unified image (#26) has no per-controller identity, "a valid frame
    // arrived" is the entire signal.
    uint8_t pkt[4];
    pkt[0] = 0xAA;
    pkt[1] = 0x55;
    pkt[2] = seq++;
    pkt[3] = crc8(&pkt[2], 1);

    _ser.write(pkt, sizeof(pkt));

}

bool Heartbeat::peerAlive(uint32_t nowMs, uint32_t timeoutMs) const{
    if (_lastRxMs == 0) return false;
    return (uint32_t)(nowMs - _lastRxMs) <= timeoutMs;
}

void Heartbeat::tick(){
    while(_ser.available() > 0){
        parseByte((uint8_t)_ser.read());
    }
}

void Heartbeat::parseByte(uint8_t b){
    // Frame: AA 55 SEQ CRC (must match send() above)
    switch(_state){
        case 0:
            _state = (b == 0xAA) ? 1 : 0;
            break;
        case 1:
            _state = (b == 0x55) ? 2 : 0;
            break;
        case 2:
            _seq = b;
            _state = 3;
            break;
        case 3: {
            if (b == crc8(&_seq, 1)){
                _lastRxMs = millis();
            }
            _state = 0;
        } break;

        default:
            _state = 0;
            break;
    }
}