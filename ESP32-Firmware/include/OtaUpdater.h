#pragma once

#include <Arduino.h>

class OtaUpdater {
    public:
        void begin();
        void request(const char* url, const char* sha256Hex);
        void tick();
        void confirmHealthy();

        // True while a blocking update is running. The heartbeat task reads this
        // to go quiet so the partner takes over during the update + reboot.
        // static: callable as OtaUpdater::isUpdating() without an instance.
        static bool isUpdating();

    private:
        enum State {IDLE, REQUESTED};
        State _state = IDLE;

        char _url[128];
        char _sha256Hex[65];

        // Probation: the stock Arduino bootloader is built without
        // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, so an OTA'd image boots
        // straight to VALID and nothing reverts it if it turns out broken.
        // We do that job in software: a freshly flashed image is on trial
        // until it reaches MQTT, and reverts to the previous slot if it
        // cannot, or if it keeps rebooting before it gets there.
        bool _probation = false;
        uint32_t _probationDeadlineMs = 0;

        void markProbation();                        // arm before rebooting into a new image
        void revertToPrevious(const char* why);      // boot the other slot and restart

        // The whole blocking download-verify-flash-reboot sequence.
        void runUpdate();
};