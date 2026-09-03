#include "OtaUpdater.h"

#include <Ethernet.h>
#include <Update.h>            // Arduino flash writer
#include <Preferences.h>       // NVS wrapper, holds the probation state
#include "mbedtls/sha256.h"

#ifdef ESP32
  #include <esp_ota_ops.h>    
#endif

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------
static const uint32_t OTA_CONNECT_TIMEOUT_MS = 5000;
static const uint32_t OTA_STALL_MS           = 10000;
static const size_t   OTA_CHUNK              = 1024;

// Probation window: how long a freshly flashed image has to reach MQTT
// before we give up on it, and how many boots it may burn trying.
static const uint32_t OTA_PROBATION_MS   = 60000;
static const uint8_t  OTA_MAX_BOOT_TRIES = 3;

// NVS namespace and keys for the probation state (survives reset and power loss).
static const char* OTA_NVS_NS    = "ota";
static const char* OTA_KEY_PROB  = "probation";
static const char* OTA_KEY_BOOTS = "boots";

// One-way "OTA running" flag: written here (core 0), read by heartbeatTask (core 1).
static volatile bool s_inProgress = false;


bool OtaUpdater::isUpdating(){ return s_inProgress;}

// ===========================================================================
// File-local helpers (write these as `static` free functions)
// ===========================================================================

//   split "http://host:port/path"; default port 80; default path "/"
static bool parseUrl(const char* url, char* host, size_t hostSz, uint16_t& port, char* path, size_t pathSz){
    const char* p = url;

    const char* scheme = "http://";
    if(strncmp(p, scheme, strlen(scheme)) != 0) return false;
    p += strlen(scheme);

    size_t hi = 0;
    while(*p && *p != ':' && *p != '/'){
        if(hi +1 >= hostSz) return false;
        host[hi++] = *p++;
    }
    host[hi] = '\0';
    if(hi == 0) return false;

    port = 80;
    if(*p == ':'){
        p++;
        uint32_t val = 0;
        if(*p <'0' || *p > '9') return false;
        while(*p >= '0' && *p <= '9'){
            val = val *10 + (uint32_t)(*p -'0');
            p++;
        }
        if(val == 0 || val > 65535) return false;
        port = (uint16_t)val;
    }

    if (*p == '\0'){
        if(pathSz < 2) return false;
        path[0] = '/'; path[1] = '\0';
    } else {
        size_t pi = 0;
        while(*p) {
            if(pi + 1 >= pathSz) return false;
            path[pi++] = *p++;
        } 
        path[pi] = '\0';
    }
    return true;
}

//   read until '\n', drop '\r', return length or -1 on stall/disconnect
static int readLine(EthernetClient& client, char* buf, size_t bufSz){
    size_t i = 0;
    uint32_t lastData = millis();
    while(true){
        if(client.available()){
            int c = client.read();
            if(c<0) continue;
            lastData = millis();
            if(c == '\n') break;
            if(c == '\r') continue;
            if(i+1 < bufSz) buf[i++] = (char)c;
        } else {
            if(!client.connected() && !client.available()) return -1;
            if(millis() - lastData > OTA_STALL_MS) return -1;
            delay(1);
        }
    }
    buf[i] = '\0';
    return (int)i;
}

// 32 bytes to 64 lowercase hex chars
static void toHex(const uint8_t* digest, size_t n, char* out){
    static const char* hexd = "0123456789abcdef";
    for(size_t i=0; i < n;i++){
        out[i*2] = hexd[(digest[i] >> 4) & 0x0F];
        out[i*2+1] = hexd[(digest[i]& 0x0F)];
    }
    out[n*2] = '\0';
}

static void logOtaState(){
    #ifdef ESP32
        const esp_partition_t* running = esp_ota_get_running_partition();
        esp_ota_img_states_t st;
        if(running && esp_ota_get_state_partition(running, &st) == ESP_OK){
            Serial.printf("[OTA] Running partition '%s', img_state=%d\n", running->label, (int)st);
        } else if(running){
            Serial.printf("[OTA] Running partition %s (img_state unavailable)\n", running->label);
        }
    #endif
}

// ===========================================================================
// Public methods
// ===========================================================================

void OtaUpdater::begin() {
  logOtaState();

  // Pick up probation state left by a previous OTA. Every boot that happens
  // while on probation counts against the image: an image that panics early
  // burns its tries and gets reverted without ever reaching the deadline.
  Preferences prefs;
  if (!prefs.begin(OTA_NVS_NS, false)) {
    Serial.println("[OTA] NVS unavailable, probation disabled this boot");
    return;
  }

  if (prefs.getBool(OTA_KEY_PROB, false)) {
    const uint8_t tries = prefs.getUChar(OTA_KEY_BOOTS, 0) + 1;
    prefs.putUChar(OTA_KEY_BOOTS, tries);
    prefs.end();

    if (tries > OTA_MAX_BOOT_TRIES) {
      revertToPrevious("image kept rebooting without reaching MQTT");
      return;  // not reached, revertToPrevious restarts
    }

    _probation = true;
    _probationDeadlineMs = millis() + OTA_PROBATION_MS;
    Serial.printf("[OTA] Image on probation (boot %u of %u), must reach MQTT within %lu ms\n",
                  tries, OTA_MAX_BOOT_TRIES, (unsigned long)OTA_PROBATION_MS);
    return;
  }

  prefs.end();
}

// Arm probation for the image we are about to reboot into.
void OtaUpdater::markProbation() {
  Preferences prefs;
  if (!prefs.begin(OTA_NVS_NS, false)) {
    Serial.println("[OTA] WARNING: NVS unavailable, new image will not be on probation");
    return;
  }
  prefs.putBool(OTA_KEY_PROB, true);
  prefs.putUChar(OTA_KEY_BOOTS, 0);
  prefs.end();
  Serial.println("[OTA] New image armed for probation");
}

// Point the bootloader back at the slot we are not running and restart.
// With two app slots, "next update partition" is by definition the other one,
// which is the image we came from.
void OtaUpdater::revertToPrevious(const char* why) {
  Serial.printf("[OTA] ROLLBACK: %s\n", why);

  // Clear probation first so the image we fall back to boots clean, even if
  // the partition switch below fails and we end up staying where we are.
  Preferences prefs;
  if (prefs.begin(OTA_NVS_NS, false)) {
    prefs.putBool(OTA_KEY_PROB, false);
    prefs.putUChar(OTA_KEY_BOOTS, 0);
    prefs.end();
  }
  _probation = false;

#ifdef ESP32
  const esp_partition_t* prev = esp_ota_get_next_update_partition(NULL);
  if (prev == nullptr) {
    Serial.println("[OTA] ROLLBACK FAILED: no other app partition found");
    return;
  }

  const esp_err_t err = esp_ota_set_boot_partition(prev);
  if (err != ESP_OK) {
    Serial.printf("[OTA] ROLLBACK FAILED: %s\n", esp_err_to_name(err));
    return;
  }

  Serial.printf("[OTA] Reverting to '%s', rebooting...\n", prev->label);
  delay(100);
  ESP.restart();
#endif
}

// Called from the main loop once MQTT is connected: the image works well
// enough to talk to the host, so end its probation and keep it.
void OtaUpdater::confirmHealthy() {
    static bool confirmed = false;
    if(confirmed) return;
    confirmed=true;

    // Clear our software probation. This is the part that actually protects
    // us, since the stock bootloader never marks images PENDING_VERIFY.
    if (_probation) {
        Preferences prefs;
        if (prefs.begin(OTA_NVS_NS, false)) {
            prefs.putBool(OTA_KEY_PROB, false);
            prefs.putUChar(OTA_KEY_BOOTS, 0);
            prefs.end();
        }
        _probation = false;
        Serial.println("[OTA] Image reached MQTT, probation passed and cleared");
    }

  #ifdef ESP32
    // Bootloader-level rollback, only meaningful if the bootloader was built
    // with CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE. The stock PlatformIO
    // Arduino bootloader is not, so this normally reports a normal boot.
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if(running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY){
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        Serial.printf("[OTA] Pending image confirmed valid, rollback canceled: %s\n", esp_err_to_name(err));
    } else {
        Serial.println("[OTA] No bootloader-pending image to confirm");
    }
  #endif
}

void OtaUpdater::request(const char* url, const char* sha256Hex) {
  //       reject null / non-64-char hash; snprintf-copy into _url / _sha256Hex;
  //       set _state = REQUESTED
  if(url == nullptr || sha256Hex == nullptr){
    Serial.println("[OTA] request rejected: null argument");
    return;
  }
  if(strlen(sha256Hex)!=64){
    Serial.printf("[OTA] request rejected: sha must be 64 hex chars, got %u\n", (unsigned)strlen(sha256Hex));
    return;
  }
  snprintf(_url, sizeof(_url), "%s", url);
  snprintf(_sha256Hex, sizeof(_sha256Hex), "%s", sha256Hex);
  _state = REQUESTED;
  Serial.printf("[OTA] Update requested: %s\n", _url);
}

void OtaUpdater::tick() {
    // Probation deadline: if this image has not reached MQTT in time, it does
    // not work well enough to keep. Signed subtraction so millis() rollover
    // cannot make the deadline look permanently unreached.
    if (_probation && (int32_t)(millis() - _probationDeadlineMs) >= 0) {
        revertToPrevious("no MQTT connection within the probation window");
        return;  // not reached, revertToPrevious restarts
    }

    if(_state != REQUESTED) return;
    _state = IDLE;
    runUpdate();
}


// ===========================================================================
// The blocking sequence. Success reboots (never returns). Any failure logs,
// releases the socket, clears the flag, leaves the running image untouched.
// ===========================================================================
void OtaUpdater::runUpdate() {
    Serial.printf("[OTA] Starting update from %s\n", _url);

  // Phase 0: s_inProgress = true; define a fail() cleanup lambda
    s_inProgress = true;

    auto fail = [&](const char* why){
        Serial.printf("[OTA] ABORT: %s\n", why);
        Update.abort();
        s_inProgress = false;
    };

  // Phase 1: parseUrl -> host/port/path
    char host[64];
    char path[128];
    uint16_t port = 80;
    if(!parseUrl(_url, host, sizeof(host), port, path, sizeof(path))){
        fail("bad url"); return;
    }

  // Phase 2: EthernetClient client; connect
    EthernetClient client;
    client.setConnectionTimeout(OTA_CONNECT_TIMEOUT_MS);
    Serial.printf("[OTA] Connecting to %s:%u ...\n", host, port);
    if(!client.connect(host, port)){
        fail("connect failed"); return;
    }

  // Phase 3: send "GET <path> HTTP/1.0\r\nHost: <host>\r\nConnection: close\r\n\r\n"
    client.printf("GET %s HTTP/1.0\r\n", path);
    client.printf("Host: %s\r\n", host);
    client.print("Connection: close\r\n");
    client.print("\r\n");

  // Phase 4: read status line, require " 200 "
    char line[256];
    if(readLine(client, line, sizeof(line)) < 0){
        client.stop();
        fail("no status line"); return;
    }
    if(strstr(line, " 200 ") == nullptr){
        Serial.printf("[OTA] Server said: %s\n", line);
        client.stop();
        fail("non-200 status"); return;
    }

  // Phase 5: read headers, capture Content-Length, stop at blank line
    long contentLength = -1;
    while(true){
        int n = readLine(client, line, sizeof(line));
        if(n<0) {client.stop(); fail("header read fail"); return;}
        if(n==0) break;
        if(strncasecmp(line, "Content-Length:", 15) == 0){
            contentLength = atol(line + 15);
        }
    }
    if(contentLength <= 0){
        client.stop(); fail("missing/zero Content-Length"); return;
    }
    Serial.printf("[OTA] Downloading %ld bytes\n", contentLength);

  // Phase 6: Update.begin(contentLength)
    if(!Update.begin((size_t)contentLength)){
        Serial.printf("[OTA] Update.begin error: %s\n", Update.errorString());
        client.stop(); fail("Update.begin failed"); return;
    }
  
  // Phase 7: mbedtls_sha256_init/starts_ret
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

  // Phase 8: stream loop: read chunk -> Update.write + sha256_update_ret,
  //          stall timer, stop at contentLength
    static uint8_t buf[OTA_CHUNK];
    long received = 0;
    uint32_t lastData = millis();
    while(received < contentLength){
        int avail = client.available();
        if(avail<=0){
            if(!client.connected() && client.available() <= 0){
                mbedtls_sha256_free(&sha); client.stop();
                fail("Connection closed early"); return;
            }
            if(millis() - lastData > OTA_STALL_MS){
                mbedtls_sha256_free(&sha); client.stop();
                fail("stalled"); return;
            }
            delay(1);
            continue;
        }

        size_t want = (size_t)avail;
        if(want > OTA_CHUNK) want = OTA_CHUNK;
        if((long)want > contentLength - received) want = (size_t)(contentLength - received);

        int got = client.read(buf, want);
        if(got <= 0) continue;
        lastData = millis();

        if(Update.write(buf, got)!= (size_t)got){
            Serial.printf("[OTA] flash write error: %s\n", Update.errorString());
            mbedtls_sha256_free(&sha); client.stop();
            fail("flash write failed"); return;
        }
        mbedtls_sha256_update_ret(&sha, buf, got);
        received += got;        
    }

  // Phase 9: sha256_finish_ret; compare to _sha256Hex; mismatch -> abort, stay
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    client.stop();

    char gotHex[65];
    toHex(digest, sizeof(digest), gotHex);
    if(strcmp(gotHex, _sha256Hex)!=0){
        Serial.printf("[OTA] hash mismatch\n expected       %s\n got        %s\n", _sha256Hex, gotHex);
        fail("hash mismatch"); return;
    }

  // Phase 10: Update.end(true); ESP.restart()
    if(!Update.end(true)){
        Serial.printf("[OTA] Update.end error: %s\n", Update.errorString());
        fail("Update.end failed"); return;
    }

    // Put the incoming image on trial before we hand control to it.
    markProbation();

    Serial.println("[OTA] Update OK, rebooting into new image...");
    delay(100);
    ESP.restart();
}
