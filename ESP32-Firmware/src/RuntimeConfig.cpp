#include "RuntimeConfig.h"

#include <ArduinoJson.h>

static String normalizeMac(const String& in){
    String out;
    for(size_t i =0; i<in.length(); i++){
        char c = in[i];
        if(c == ':' || c == '-') continue;
        out += (char)toupper(c);
    }
    return out;
}

ControllerRole parseRole(const String& roleText){
    if(roleText == "Primary") return ControllerRole::Primary;
    if(roleText == "Standby") return ControllerRole::Standby;
    return ControllerRole::Unknown;
}

String roleToString(ControllerRole role){
    switch(role){
        case ControllerRole::Primary:
            return "Primary";

        case ControllerRole::Standby:
            return "Standby";
        
        default:
            return "Unknown";
    }
}

bool RuntimeConfig::isPrimary() const{
    return role == ControllerRole::Primary;
}

bool RuntimeConfig::isStandby() const{
    return role == ControllerRole::Standby;
}

const char* RuntimeConfig::roleString() const{
    switch (role){
        case ControllerRole::Primary:
            return "Primary";
        
        case ControllerRole::Standby:
            return "Standby";
    
        default:
            return "Unknown";
        }
}

bool RuntimeConfig::isValid(const String& device_mac) const{
    if(!configured) return false;
    if(normalizeMac(mac) != normalizeMac(device_mac)) return false;
    if(rackID.length() == 0) return false;
    if(role == ControllerRole::Unknown) return false;
    
    return true;
}

bool parseRuntimeConfigJson(const String& json, RuntimeConfig& outConfig, String& error, const String& device_mac){
    StaticJsonDocument<2048> doc;

    DeserializationError parseError = deserializeJson(doc, json);

    if(parseError){
        error = String("JSON parse failed: ") + parseError.c_str();
        return false;
    }

    const char* messageType = doc["message_type"] | "";

    if(String(messageType) != "config"){
        error = "message type is not config";
        return false;
    }

    RuntimeConfig cfg;

    cfg.configured = doc["configured"] | false;
    cfg.enabled = doc["enabled"] | false;

    cfg.mac = String((const char*)(doc["mac"] | ""));
    cfg.rackID = String((const char*)(doc["rack_id"] | ""));

    String roleText = String((const char*)(doc["role"] | ""));
    cfg.role = parseRole(roleText);

    JsonObject topics = doc["topics"];
    cfg.telemetryTopic = String((const char*)(topics["telemetry"] | ""));
    cfg.statusTopic = String((const char*)(topics["status"] | ""));
    cfg.eventTopic = String((const char*)(topics["event"] | ""));

    if(!cfg.isValid(device_mac)){
        error = "config failed validation step";

        if(!cfg.configured){
            error = "Device is not configured";
        }else if(normalizeMac(cfg.mac) != normalizeMac(device_mac)){
            error = "MAC does not match with this device";
        }else if(cfg.rackID.length() == 0){
            error = "No rack id";
        }else if(cfg.role == ControllerRole::Unknown){
            error = "Invalid or Unknown role";
        }else if(cfg.telemetryTopic.length()==0){
            error = "Missing telemetry topic";
        }

        return false;
    }

    outConfig = cfg;
    error = "";

    return true;
}