#pragma once
#include <Arduino.h>

enum class ControllerRole{
    Unknown,
    Primary,
    Standby
};

struct RuntimeConfig{
    bool configured = false;
    bool enabled = false;

    String mac;
    String rackID;
    ControllerRole role = ControllerRole::Unknown;

    String telemetryTopic;
    String statusTopic;
    String eventTopic;

    String lastError;

    bool isValid() const;
    bool isPrimary() const;
    bool isStandby() const;

    const char* roleString() const; //If not used in the future remove it
};

bool parseRuntimeConfigJson(const String& json, RuntimeConfig& outConfig, String& error);

//Helpers
ControllerRole parseRole(const String& roleText);
String roleToString(ControllerRole role);