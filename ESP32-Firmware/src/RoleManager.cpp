#include "RoleManager.h"
#include "DeviceConfig.h"
#include "TimeUtil.h"

RoleManager::RoleManager(Heartbeat& hb, char myId) : _hb(hb), _myId(myId){}

// TODO: Remove the logic device ID and use Role as the main source of the decision logic.
//       Primary sends the 

void RoleManager::begin(){
    //Deterministic Startup:
    // A starts active, B starts standby
    if(_myId == 'A'){
        becomeActive();
    } else {
        becomeStandby();
    }
}

void RoleManager::becomeActive(){
    _state = RoleState::PRIMARY_ACTIVE;
    // _relays.setOwner((_myId == 'A') ? BusOwner::A : BusOwner::B);
}

void RoleManager::becomeStandby(){
    _state = RoleState::STANDBY_PASSIVE;
    // _relays.setOwner(BusOwner::NONE);
}

void RoleManager::tick(){
    uint32_t now = millis();

    _hb.tick(); // Always parse incoming heartbeats

    if(elapsed(now, _lastSendMs, HB_SEND_MS)){
        _lastSendMs = now;
        _hb.send(now);
    }

    bool peerAlive = _hb.peerAlive(now, HB_TIMEOUT_MS);

    switch (_state){
        case RoleState::PRIMARY_ACTIVE:
            //Preferred primary stays active
            break;

        case RoleState::STANDBY_PASSIVE:
            if(!peerAlive){
                _state = RoleState::TAKING_OVER;
                _takeOverStartMs = now;
                
                //Claim the bus
                // _relays.setOwner((_myId == 'A') ? BusOwner::A : BusOwner::B);
            }
            break;

        case RoleState::TAKING_OVER:
            //Allowing bus to settle
            if(elapsed(now, _takeOverStartMs, 200)){
                _state = RoleState::ACTIVE_AFTER_TAKEOVER;
                _takeOverStartMs = now;
            }
            break;
        
        case RoleState::ACTIVE_AFTER_TAKEOVER:
            // Hold ownership to prevent flapping
            if(elapsed(now, _takeOverStartMs, HB_TAKEOVER_HOLD_MS)){
                // Future policies -> TBD!!
            }
            break;
    }


}