#include <OneWire.h>
#include <DallasTemperature.h>
#include "TemperatureBus.h"

// DS18B20 conversion time depends on resolution.
// 12-bit worst case is ~750ms. Using 750ms keeps things safe unless you explicitly lower resolution.
static const uint32_t CONVERSION_MS = 750;

// Sample every 5 seconds
static const uint32_t SAMPLE_PERIOD_MS = 5000;

bool TemperatureBus::begin(int intakePin, int exhaustPin){
    _intakePin = intakePin;
    _exhaustPin = exhaustPin;

    auto* owI = new OneWire(_intakePin);
    auto* owE = new OneWire(_exhaustPin);
    _oneWireIntake = owI;
    _oneWireExhaust = owE;

    auto* dtI = new DallasTemperature(owI);
    auto* dtE = new DallasTemperature(owE);
    _dtIntake = dtI;
    _dtExhaust = dtE;

    dtI->begin();
    dtE->begin();

    // non-blocking conversions (we wait in tick())
    dtI->setWaitForConversion(false);
    dtE->setWaitForConversion(false);

    // how many sensors are on each bus
    if(!scanBuses()) return false;

    // reset state
    _state = IDLE;
    _lastSampleMs = 0;
    _lastRequestMs = 0;
    _newSample = false;

    // clear last readings
    for(uint8_t i=0;i<SENSORS_PER_BUS;i++){
        _intakeC[i] = NAN;
        _exhaustC[i] = NAN;
    }
    _intakePrimaryC = NAN;
    _exhaustPrimaryC = NAN;

    return true;
}

bool TemperatureBus::scanBuses(){
    auto* dtI = static_cast<DallasTemperature*>(_dtIntake);
    auto* dtE = static_cast<DallasTemperature*>(_dtExhaust);

    const uint8_t previousIntakeCount = _intakeDeviceCount;
    const uint8_t previousExhaustCount = _exhaustDeviceCount;

    // DallasTemperature caches device counts. Re-run begin() so sensors added
    // after boot are discovered during the periodic scan in tick().
    dtI->begin();
    dtE->begin();

    _intakeDeviceCount = dtI->getDeviceCount();
    _exhaustDeviceCount = dtE->getDeviceCount();

    if (_intakeDeviceCount != previousIntakeCount ||
        _exhaustDeviceCount != previousExhaustCount) {
        Serial.printf("[TEMP:scan] intakeN=%u exhaustN=%u\n",
                      _intakeDeviceCount,
                      _exhaustDeviceCount);
    }

    if(_intakeDeviceCount == 0 && _exhaustDeviceCount == 0){
        Serial.printf("[SENSOR BUS] NO device detected on both busses!\n");
        return false;
    }

    // If one side has 0 on early bring-up that's ok, but "ready()" will remain false
    // until we successfully read something on each bus.
    return true;
}

void TemperatureBus::tick(uint32_t nowMs){
    switch(_state){
        case IDLE:
            if(_lastSampleMs == 0 || (uint32_t)(nowMs - _lastSampleMs) >= SAMPLE_PERIOD_MS){
                // Refresh counts occasionally in case sensors were unplugged/added
                scanBuses();

                requestConversion();
                _lastRequestMs = nowMs;
                _state = REQUESTED;
            }
            break;

        case REQUESTED:
            if((uint32_t)(nowMs - _lastRequestMs) >= CONVERSION_MS){
                _state = READ_READY;
            }
            break;

        case READ_READY:
            readTemperatures();
            _lastSampleMs = nowMs;
            _newSample = true;
            _state = IDLE;
            break;
    }
}

void TemperatureBus::requestConversion(){
    auto* dtI = static_cast<DallasTemperature*>(_dtIntake);
    auto* dtE = static_cast<DallasTemperature*>(_dtExhaust);

    // Kick both buses at the same time so the sample is coherent.
    dtI->requestTemperatures();
    dtE->requestTemperatures();
}

void TemperatureBus::readTemperatures(){
    auto* dtI = static_cast<DallasTemperature*>(_dtIntake);
    auto* dtE = static_cast<DallasTemperature*>(_dtExhaust);

    // Read up to 3 sensors from each bus by index.
    // NOTE: index ordering is not guaranteed stable across power cycles.
    // If you need stable "top/mid/bottom" identities, bind by ROM address instead.
    for(uint8_t i=0;i<SENSORS_PER_BUS;i++){
        float tI = NAN;
        float tE = NAN;

        if(_intakeDeviceCount > i){
            tI = dtI->getTempCByIndex(i);
            if(tI <= -120.0f) tI = NAN;
        }
        if(_exhaustDeviceCount > i){
            tE = dtE->getTempCByIndex(i);
            if(tE <= -120.0f) tE = NAN;
        }
        

        _intakeC[i]  = tI;
        _exhaustC[i] = tE;
    }

    // Backward-compatible primary values (index 0)
    _intakePrimaryC  = _intakeC[0];
    _exhaustPrimaryC = _exhaustC[0];
}

bool TemperatureBus::ready() const{
    return !isnan(_intakePrimaryC) && !isnan(_exhaustPrimaryC);
}

bool TemperatureBus::hasNewSample() const{
    return _newSample;
}

void TemperatureBus::clearNewSampleFlag() {
    _newSample = false;
}
