#include "TelemetrySender.h"
#include "HostConfig.h"
#include "DeviceConfig.h"
#include "RuntimeConfig.h"

#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>

#ifdef ESP32
  #include <esp_mac.h>
#endif

static EthernetClient ethClient;
static bool gEthUp = false;
static char gMacSafe[13] = {0};
static char gClientId[32] = {0};
static char gConfigTopic[80] = {0};
static char gHelloTopic[80] = {0};
static char gTelemetryTopic[80] = {0};
static PubSubClient mqttClient(ethClient);

static TelemetrySender* gself = nullptr;

static IPAddress hostIP() {
  return IPAddress(HOST_IP_A, HOST_IP_B, HOST_IP_C, HOST_IP_D);
}

static void printMac(const byte mac[6]) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void macAddrCompact(const byte mac[6], char* out, size_t outSz) {
  snprintf(out, outSz, "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void buildMqttIdentity(const byte mac[6]) {
  macAddrCompact(mac, gMacSafe, sizeof(gMacSafe));
  snprintf(gClientId, sizeof(gClientId), "%s-%s", BASE_TOPIC, gMacSafe);
  snprintf(gHelloTopic, sizeof(gHelloTopic), "%s/devices/%s/hello", BASE_TOPIC, gMacSafe);
  snprintf(gConfigTopic, sizeof(gConfigTopic), "%s/devices/%s/config", BASE_TOPIC, gMacSafe);
  snprintf(gTelemetryTopic, sizeof(gTelemetryTopic), "%s/devices/%s/telemetry", BASE_TOPIC, gMacSafe);         
}

static bool connectMqtt() {
  if (gClientId[0] == '\0') {
    Serial.println("[MQTT] Client ID has not been initialized");
    return false;
  }

  if (mqttClient.connected()) {
    return true;
  }

  if (!mqttClient.connect(gClientId)) {
    Serial.printf("[MQTT] Unable to connect %s, state=%d\n",
                  gClientId, mqttClient.state());
    return false;
  }
  
  Serial.printf("[MQTT] Connected as %s\n", gClientId);
  mqttClient.subscribe(gConfigTopic); // TIP: Checking the connection might be a good idea
  
  return true;
}

static const char* hardwareStatusName(EthernetHardwareStatus status) {
  switch (status) {
    case EthernetNoHardware: return "no hardware";
    case EthernetW5100: return "W5100";
    case EthernetW5200: return "W5200";
    case EthernetW5500: return "W5500";
    default: return "unknown";
  }
}

static const char* linkStatusName(EthernetLinkStatus status) {
  switch (status) {
    case Unknown: return "unknown";
    case LinkON: return "on";
    case LinkOFF: return "off";
    default: return "unknown";
  }
}

static void resetW5500IfConfigured() {
  if (W5500_RST_PIN < 0) {
    Serial.println("[NET] W5500 reset pin not configured");
    return;
  }

  Serial.printf("[NET] Resetting W5500 on GPIO %d\n", W5500_RST_PIN);
  pinMode(W5500_RST_PIN, OUTPUT);
  digitalWrite(W5500_RST_PIN, LOW);
  delay(50);
  digitalWrite(W5500_RST_PIN, HIGH);
  delay(250);
}

void onMqttMessage(char* topic, byte* payload, unsigned int lenght){
  char json[1024];
  if(lenght >= sizeof(json)){
    Serial.printf("[MQTT] Config too large: %u bytes\n", lenght);
    return;
  }

  memcpy(json, payload, lenght);
  json[lenght] = '\0';

  if(strcmp(topic, gConfigTopic) != 0){
    Serial.printf("[MQTT] Ignoring message on %s\n", topic);
    return;
  }

  RuntimeConfig cfg;
  String err;
  if(!parseRuntimeConfigJson(String(json), cfg, err)){
    Serial.printf("[CONFIG] Rejected: %s\n", err.c_str());
    return;
  }

  if(gself){
    gself -> setConfig(cfg);
    Serial.println("[CONFIG] Applied");
  }
}

String TelemetrySender::deviceMacString() {
  uint8_t mac[6] = {0};

  #ifdef ESP32
    // ESP32 "base MAC" is stable and unique per chip.
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
  #endif

  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}


// Bring up Ethernet and prepare the MQTT client.
bool TelemetrySender::begin() {
  resetW5500IfConfigured();
  gself = this;

  // Initialize SPI for W5500
  SPI.begin(W5500_SCK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);
  Ethernet.init(W5500_CS_PIN);

  // W5500 requires a MAC for Ethernet.begin (even if we use DHCP).
  // We'll derive one from the ESP32 base MAC.
  uint8_t base[6] = {0};
  #ifdef ESP32
    esp_read_mac(base, ESP_MAC_WIFI_STA);
  #endif
  byte ethMac[6] = { base[0], base[1], base[2], base[3], base[4], base[5] };
  buildMqttIdentity(ethMac);

  Serial.println("[NET] Initializing W5500...");
  Serial.print("[NET] MAC=");
  printMac(ethMac);
  Serial.println();

  Serial.println("[NET] Starting DHCP...");
  const int dhcpResult = Ethernet.begin(ethMac);

  Serial.print("[NET] Hardware=");
  const EthernetHardwareStatus hardwareStatus = Ethernet.hardwareStatus();
  Serial.println(hardwareStatusName(hardwareStatus));
  if (hardwareStatus == EthernetNoHardware) {
    gEthUp = false;
    Serial.println("[NET] W5500 not detected on SPI. Check 3V3, GND, SCK, MISO, MOSI, CS, and RST.");
    return false;
  }

  Serial.print("[NET] Link after init=");
  Serial.println(linkStatusName(Ethernet.linkStatus()));

  if (dhcpResult == 0) {
    gEthUp = false;
    Serial.println("[NET] DHCP failed. Telemetry will not send.");
    return false;
  }

  delay(200);

  if (Ethernet.linkStatus() == LinkON) {
    gEthUp = true;
    Serial.println("[NET] Link up. DHCP lease acquired.");

    Serial.print("[NET] Local IP=");
    Serial.println(Ethernet.localIP());

    Serial.print("[NET] Subnet=");
    Serial.println(Ethernet.subnetMask());

    Serial.print("[NET] Gateway=");
    Serial.println(Ethernet.gatewayIP());

    Serial.print("[NET] DNS=");
    Serial.println(Ethernet.dnsServerIP());

    Serial.print("[NET] MQTT Destination=");
    Serial.print(hostIP());
    Serial.print(":");
    Serial.println((uint16_t)MQTT_PORT);

    Serial.println("[MQTT] Setting up MQTT server");
    mqttClient.setServer(hostIP(), MQTT_PORT);
    mqttClient.setBufferSize(1024);
    mqttClient.setCallback(onMqttMessage);
    connectMqtt();
    

  } else {
    gEthUp = false;
    Serial.println("[NET] Link DOWN (check cable/switch). Telemetry will not send.");
    return false;
  }

  return gEthUp;
}

void TelemetrySender::loop() {
  if (isUp() && mqttClient.connected()) {
    mqttClient.loop();
  }
}

bool TelemetrySender::isUp() const {
  // Update link status opportunistically.
  gEthUp = (Ethernet.linkStatus() == LinkON);
  return gEthUp;
}

void TelemetrySender::setConfig(const RuntimeConfig &cfg){
  config_ = cfg;
}


bool TelemetrySender::hello(){
  if(config_.configured) return false;
  if(!isUp() || !connectMqtt()) return false;

  uint32_t now = millis();
  static uint32_t lastHelloMs = 0;
  if(now - lastHelloMs < HELLO_INTERVAL_MS) return false;
  lastHelloMs = now;

  char payload[64];
  snprintf(payload, sizeof(payload), "{\"message_type\":\"hello\",\"mac\":\"%s\"}", gMacSafe);
  bool published = mqttClient.publish(gHelloTopic, payload);
  Serial.printf("[HELLO] Published to %s with a payload of %s\n", gHelloTopic, payload);
  if(!published){
    Serial.printf("[HELLO] Publish failed, state=%d\n", mqttClient.state());
  }

  return published;
}


bool TelemetrySender::sendMQTT(const char* jsonPayload){
  if (!isUp()) return false;
  if (!jsonPayload || !jsonPayload[0]) return false;
  if (gTelemetryTopic[0] == '\0') return false;
  if (!connectMqtt()) return false;
  if(!config_.configured) return false;
  if(!config_.enabled) return false;

  const bool published = mqttClient.publish(gTelemetryTopic, jsonPayload);
  if (!published) {
    Serial.printf("[MQTT] Telemetry publish failed on %s, state=%d\n",
                  gTelemetryTopic, mqttClient.state());
  }
  return published;
}

bool TelemetrySender::sendEvent(const char* jsonPayload){
  if (!jsonPayload || !jsonPayload[0]) return false;
  if (config_.eventTopic[0] == '\0') return false;
  if (!connectMqtt()) return false;
  if(!config_.enabled) return false;

  const bool published = mqttClient.publish(config_.eventTopic.c_str(), jsonPayload);
  if (!published) {
    Serial.printf("[MQTT] Event publish failed on %s, state=%d\n",
                  config_.eventTopic.c_str(), mqttClient.state());
  }
  return published;
}
