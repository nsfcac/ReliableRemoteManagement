#include <Arduino.h>
#include "DeviceConfig.h"
#include "HostConfig.h"
#include "Heartbeat.h"
#include "TemperatureBus.h"
#include "TelemetrySender.h"

HardwareSerial HBSerial(HB_UART_NUM);
Heartbeat hb(HBSerial);
TemperatureBus tempBus;
TelemetrySender net;

static inline bool isControllerPrimary() { return net.isConfigured() && net.role() == ControllerRole::Primary; }
static inline bool isControllerStandby() { return net.isConfigured() && net.role() == ControllerRole::Standby; }

void heartbeatTask(void *param){
  uint32_t lastSend = 0;
  
  while(true){
    const uint32_t now = millis();

    //Always read the incoming data
    hb.tick();

    //Send own heartbeat periodically
    if((uint32_t)(now - lastSend) >= HB_SEND_MS){
      lastSend = now;
      hb.send(now);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void printTemps() {
  Serial.printf(
    "[TEMP] inlet: %.2f %.2f %.2f | exhaust: %.2f %.2f %.2f\n",
    tempBus.intakeC(0), tempBus.intakeC(1), tempBus.intakeC(2),
    tempBus.exhaustC(0), tempBus.exhaustC(1), tempBus.exhaustC(2)
  );
}

static void maybePrintTemps() {
  if(tempBus.intakeDeviceCount() == 0 && tempBus.exhaustDeviceCount() == 0) return;
  if (tempBus.hasNewSample()) {
    tempBus.clearNewSampleFlag();
    printTemps();
  }
}

static void appendTempArray(char* out, size_t outSz, const float vals[TemperatureBus::SENSORS_PER_BUS]) {
  // Writes something like: [21.23, 22.00, null]
  // NAN -> null
  size_t used = strnlen(out, outSz);
  if (used >= outSz) return;

  auto append = [&](const char* s) {
    size_t u = strnlen(out, outSz);
    if (u < outSz) strncat(out, s, outSz - u - 1);
  };

  append("[");
  for (uint8_t i = 0; i < TemperatureBus::SENSORS_PER_BUS; i++) {
    if (i) append(", ");
    if (isnan(vals[i])) {
      append("null");
    } else if (vals[i] == 85.0f) {
      append("null");
    } else {
      char b[16];
      snprintf(b, sizeof(b), "%.2f", (double)vals[i]);
      append(b);
    }
  }
  append("]");
}

static void buildTelemetryJson(char* out, size_t outSz,
                               const char* macStr,
                               uint32_t nowMs,
                               bool primaryAlive,
                               bool standbyAlive,
                               const float intake[TemperatureBus::SENSORS_PER_BUS],
                               const float exhaust[TemperatureBus::SENSORS_PER_BUS]) {
  // We avoid ArduinoJson to keep dependencies minimal.
  if (!out || outSz == 0) return;
  out[0] = '\0';

  // Pre-build temperature arrays
  char intakeArr[96] = {0};
  char exhaustArr[96] = {0};
  appendTempArray(intakeArr, sizeof(intakeArr), intake);
  appendTempArray(exhaustArr, sizeof(exhaustArr), exhaust);

  // Escape details minimally (replace \" with ').
  // char detailsSafe[128];
  // size_t di = 0;
  // if (!failoverDetails) failoverDetails = "";
  // for (size_t i = 0; failoverDetails[i] && di + 1 < sizeof(detailsSafe); i++) {
  //   char c = failoverDetails[i];
  //   if (c == '"') c = '\'';
  //   if ((uint8_t)c < 0x20) c = ' '; // strip control chars
  //   detailsSafe[di++] = c;
  // }
  // detailsSafe[di] = '\0';

  snprintf(
    out, outSz,
    "{\n"
    "  \"message_type\": \"telemetry\",\n\n"
    "  \"device\": {\n"
    "    \"mac\": \"%s\"\n"
    "  },\n\n"
    "  \"timestamp_device_ms\": %lu,\n\n"
    "  \"items\": [\n"
    "    {\n"
    "      \"kind\": \"heartbeat\",\n"
    "      \"primary_alive\": %s,\n"
    "      \"standby_alive\": %s\n"
    "    },\n"
    "    {\n"
    "      \"kind\": \"sensors\",\n"
    "      \"buses\": [\n"
    "        {\n"
    "          \"bus\": \"inlet\",\n"
    "          \"temperatures_c\": %s\n"
    "        },\n"
    "        {\n"
    "          \"bus\": \"exhaust\",\n"
    "          \"temperatures_c\": %s\n"
    "        }\n"
    "      ]\n"
    "    }\n"
    "  ]\n"
    "}\n",
    macStr,
    (unsigned long)nowMs,
    primaryAlive ? "true" : "false",
    standbyAlive ? "true" : "false",
    intakeArr,
    exhaustArr
  );
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.printf("Booting Controller!\n");

  // Start heartbeat UART link and create the Heartbeat task
  hb.begin(HB_RX_PIN, HB_TX_PIN, HB_UART_BAUD);
  xTaskCreatePinnedToCore(heartbeatTask, "Heartbeat", 4096, nullptr, 3, nullptr, 1);

  // Start temperature buses (intake + exhaust)
  tempBus.begin(ONE_WIRE_BUS_INTAKE, ONE_WIRE_BUS_EXHAUST);

  // Start Ethernet telemetry
  bool netOK = net.begin();
  if(!netOK){
    Serial.println("[SETUP NET] Ethernet init failed or link is down");
  }

  Serial.println("Heartbeat + TemperatureBus started\n");

  // Optional sanity check
  Serial.printf("[TEMP:init] intakeN=%u exhaustN=%u\n",
                tempBus.intakeDeviceCount(),
                tempBus.exhaustDeviceCount());
}

void loop() {
  const uint32_t now = millis();

  net.loop();
  net.hello();

  // 3) Temperature sampling (tick exactly once per loop)
  tempBus.tick(now);

  // 4) Heartbeat status
  const bool peerAlive = hb.peerAlive(now, HB_TIMEOUT_MS);
  const uint32_t ageMs = (hb.lastRxMS() == 0) ? 0 : (now - hb.lastRxMS());

  // Determine A/B alive flags (self is always alive)
  const bool primaryAlive = (isControllerPrimary()) ? true : peerAlive;
  const bool standbyAlive = (isControllerStandby()) ? true : peerAlive;

  // Determine whether B should take over sending:
  // - "healthy" means A is the peer and is alive
  const bool everSawPartner = (hb.lastRxMS() != 0);
  const bool partnerAlive = peerAlive;


  // Failover event tracking (purely logical in this "no-relay" version)
  // static bool failoverOccurred = false;
  // static char failoverDetails[96] = {0};
  // if (isControllerStandby() && !failoverOccurred) { 
  //   const bool healthy =  peerAlive;
  //   if (everSawPartner && !healthy) {
  //     failoverOccurred = true;
  //     snprintf(failoverDetails, sizeof(failoverDetails),
  //              "Standby took over after %lu ms heartbeat silence", (unsigned long)ageMs);
  //   }
  // }

  // 5) Telemetry send (only active controller sends)
  static uint32_t lastTelemetry = 0;

  // Optional boot grace: if A never appears, let B start sending after a few seconds
  const uint32_t BOOT_GRACE_MS = 10000;
  const bool allowBootTakeover = (now > BOOT_GRACE_MS) && !everSawPartner;

  bool iAmActiveSender = false;
  if (isControllerPrimary()) {
    iAmActiveSender = true;                  //Primary always sends by default
  } else if (isControllerStandby()) {
    // Standby sends only if A is unhealthy (after timeout) OR A never appeared after grace
    iAmActiveSender = (!partnerAlive && everSawPartner) || allowBootTakeover; // TODO: It need to send event accrued to /event topic
  }
  iAmActiveSender = iAmActiveSender && net.isEnabled(); // Only send if it is enabled

  if (iAmActiveSender && (uint32_t)(now - lastTelemetry) >= (uint32_t)TELEMETRY_SEND_MS) {
    lastTelemetry = now;

    float cool[TemperatureBus::SENSORS_PER_BUS];
    float exhaust[TemperatureBus::SENSORS_PER_BUS];
    for (uint8_t i = 0; i < TemperatureBus::SENSORS_PER_BUS; i++) {
      cool[i] = tempBus.intakeC(i);
      exhaust[i] = tempBus.exhaustC(i);
    }

    static char json[768];
    const String macStr = TelemetrySender::deviceMacString();
    buildTelemetryJson(json, sizeof(json),
                       macStr.c_str(), now,
                       primaryAlive, standbyAlive,
                       cool, exhaust);

    const bool ok = net.sendMQTT(json);
    if (!ok) {
      Serial.println("[NETWORK] Telemetry send failed (link down or MQTT error)");
    } else {
      Serial.println("[NETWORK] Telemetry Sent Successfully");
    }
  }

  // Enabled Controller: prints HB status periodically + always prints temps when sampled
  if (net.isEnabled()) {
    static uint32_t lastStatus = 0;
    if ((uint32_t)(now - lastStatus) >= 1000) {
      lastStatus = now;
      Serial.printf("[HB:%s] peer=%s, alive=%d, age_ms=%lu\n",
                    net.config().roleString(),
                    net.config().role == ControllerRole::Primary ? "Standby" : "Primary",
                    peerAlive ? 1 : 0,
                    (unsigned long)ageMs);
    }

    maybePrintTemps();
  }

  // Standby Controller: prints temps only when A is unhealthy; logs transitions
  if (isControllerStandby()) {
    static bool lastHealthy = false;
    static bool everHealthy = false;

    // Your original policy: B considers "healthy" only if the peer is A and alive.
    const bool healthy = peerAlive;

    if (lastHealthy && !healthy) {
      Serial.printf("[ALERT: STANDBY] Lost heartbeat from Primary (timeout=%d ms). peer=%c age_ms=%lu\n",
                    (int)HB_TIMEOUT_MS, hb.peerId(), (unsigned long)ageMs);
      
        if (net.isUp()){
          const bool sent = net.sendEvent("{\"message_type\":\"event\",\"event_type\":\"primary_down\",\"details\":\"Standby took over after Primary heartbeat timeout\"}");
          if (!sent) {
            Serial.println("[ALERT: STANDBY] Failed to send primary down event to MQTT");
          }      
        } else {
          Serial.println("[ALERT: STANDBY] Network is down. Failed to send primary down event to MQTT");
        }
      

    }

    if (!lastHealthy && healthy) {
      if (everHealthy) {
        Serial.printf("[RECOVER: STANDBY] Heartbeat from Primary restored. age_ms=%lu\n",
                      (unsigned long)ageMs);

        if (net.isUp()){
          const bool sent = net.sendEvent("{\"message_type\":\"event\",\"event_type\":\"primary_up\",\"details\":\"Standby detected Primary heartbeat restored\"}");
          if (!sent) {
            Serial.println("[RECOVER: STANDBY] Failed to send primary up event to MQTT");
          }      
        } else {
          Serial.println("[RECOVER: STANDBY] Network is down. Failed to send primary up event to MQTT");
        }

        Serial.printf("[INFO: STANDBY] Entering passive mode to save energy. Telemetry will not send.\n");
        // TODO: Enter low energy mode just listen/sent heartbeat and listen config topic.
      }
      everHealthy = true;
    }

    lastHealthy = healthy;
  }

  vTaskDelay(pdMS_TO_TICKS(10));
}
