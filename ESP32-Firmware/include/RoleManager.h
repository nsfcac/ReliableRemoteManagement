#pragma once
#include <Arduino.h>
#include "Heartbeat.h"
// #include "RelayControl.h"

enum class RoleState{
    PRIMARY_ACTIVE,
    STANDBY_PASSIVE,
    TAKING_OVER,
    ACTIVE_AFTER_TAKEOVER
};

class RoleManager{
public:
    RoleManager(Heartbeat& hb, char myId);

    void begin();
    void tick();

    RoleState state() const {return _state;}

private:
    Heartbeat& _hb;
    char _myId;

    RoleState _state = RoleState::STANDBY_PASSIVE;
    uint32_t _lastSendMs = 0;
    uint32_t _takeOverStartMs = 0;

    void becomeActive();
    void becomeStandby();
};