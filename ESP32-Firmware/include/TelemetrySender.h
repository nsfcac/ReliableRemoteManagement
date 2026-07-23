#pragma once

#include <Arduino.h>
#include "RuntimeConfig.h"

// Lightweight MQTT sender for telemetry JSON payloads over a W5500.

class TelemetrySender {
  public:
    bool begin();
    void loop();
    bool isUp() const;
    void setConfig(const RuntimeConfig& cfg);
   
    const RuntimeConfig& config() const { return config_; }

    bool isConfigured() const { return config_.configured; }
    bool isEnabled() const { return config_.enabled; }
    ControllerRole role() const { return config_.role; }

    bool hello();
    bool sendMQTT(const char* jsonPayload);
    bool sendEvent(const char* jsonPayload);
    
    // Formats ESP32 base MAC (EFUSE) as "AA:BB:CC:DD:EE:FF".
    static String deviceMacString();
  private:
    RuntimeConfig config_;
};


