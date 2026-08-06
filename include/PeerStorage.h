#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include "EEPROM.h"
#elif defined(ESP_PLATFORM)
#include "nvs.h"
#include "nvs_flash.h"
#include <cstdio>
#endif

#include <cstdint>
#include <cstring>
#include "./esp_now_midi_log.h"

#define MAC_ADDRESS_SIZE 6
#define MAX_PEERS 20

namespace enomik {

class PeerStorage {
public:
    struct Peer {
        uint8_t mac[MAC_ADDRESS_SIZE];

        bool operator==(const Peer& other) const {
            return memcmp(mac, other.mac, MAC_ADDRESS_SIZE) == 0;
        }

        bool operator==(const uint8_t* otherMac) const {
            return memcmp(mac, otherMac, MAC_ADDRESS_SIZE) == 0;
        }
    };

    PeerStorage(uint16_t eepromStartAddr = 0)
        : peerCount(0)
        , eepromAddr(eepromStartAddr)
        , initialized(false)
    {
        memset(peers, 0, sizeof(peers));
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
        snprintf(nvsKey, sizeof(nvsKey), "peers%u", static_cast<unsigned>(eepromStartAddr));
#endif
    }

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
    ~PeerStorage() {
        if (initialized) {
            nvs_close(nvsHandle);
        }
    }
#endif

    // Initialize storage and load stored peers
    bool begin(size_t eepromSize = 512) {
        if (initialized) {
            return true;
        }

#ifdef ARDUINO
        if (!EEPROM.begin(eepromSize)) {
            EspNowMidiLog::e("PeerStorage: Failed to initialize EEPROM");
            return false;
        }
#elif defined(ESP_PLATFORM)
        (void)eepromSize;

        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        if (err != ESP_OK) {
            EspNowMidiLog::e("PeerStorage: nvs_flash_init failed: %d", static_cast<int>(err));
            return false;
        }

        err = nvs_open("enomik_peer", NVS_READWRITE, &nvsHandle);
        if (err != ESP_OK) {
            EspNowMidiLog::e("PeerStorage: nvs_open failed: %d", static_cast<int>(err));
            return false;
        }
#endif

        load();
        initialized = true;

        EspNowMidiLog::i("PeerStorage: Loaded %d peers", peerCount);

        return true;
    }

    // Peer management
    bool add(const uint8_t mac[MAC_ADDRESS_SIZE]) {
        if (!initialized) {
            EspNowMidiLog::e("PeerStorage: Not initialized");
            return false;
        }

        if (isFull()) {
            EspNowMidiLog::w("PeerStorage: Maximum peers reached");
            return false;
        }

        if (exists(mac)) {
            EspNowMidiLog::w("PeerStorage: Peer already exists");
            return false;
        }

        memcpy(peers[peerCount].mac, mac, MAC_ADDRESS_SIZE);
        peerCount++;
        save();

        char macBuf[EspNowMidiLog::MAC_STR_LEN];
        EspNowMidiLog::formatMac(macBuf, sizeof(macBuf), mac);
        EspNowMidiLog::i("PeerStorage: Added peer %s (Total: %d)", macBuf, peerCount);

        return true;
    }

    bool remove(const uint8_t mac[MAC_ADDRESS_SIZE]) {
        int index = findIndex(mac);
        if (index < 0) {
            EspNowMidiLog::w("PeerStorage: Peer not found");
            return false;
        }

        return remove(index);
    }

    bool remove(int index) {
        if (index < 0 || index >= peerCount) {
            EspNowMidiLog::w("PeerStorage: Invalid index");
            return false;
        }

        EspNowMidiLog::mac("PeerStorage: Removing peer ", peers[index].mac);

        // Shift remaining peers down
        for (int i = index; i < peerCount - 1; i++) {
            peers[i] = peers[i + 1];
        }

        peerCount--;
        memset(peers[peerCount].mac, 0, MAC_ADDRESS_SIZE);
        save();

        return true;
    }

    void clear() {
        peerCount = 0;
        memset(peers, 0, sizeof(peers));
        save();
        EspNowMidiLog::i("PeerStorage: All peers cleared");
    }

    // Query methods
    bool exists(const uint8_t mac[MAC_ADDRESS_SIZE]) const {
        return findIndex(mac) >= 0;
    }

    const uint8_t* get(int index) const {
        if (index < 0 || index >= peerCount) {
            return nullptr;
        }
        return peers[index].mac;
    }

    int count() const { return peerCount; }
    bool isEmpty() const { return peerCount == 0; }
    bool isFull() const { return peerCount >= MAX_PEERS; }

    // Iteration support
    const Peer* begin() const { return peers; }
    const Peer* end() const { return peers + peerCount; }

    // Debug
    void printAll() const {
        EspNowMidiLog::i("PeerStorage: Stored peers (%d):", peerCount);

        for (int i = 0; i < peerCount; i++) {
            char macBuf[EspNowMidiLog::MAC_STR_LEN];
            EspNowMidiLog::formatMac(macBuf, sizeof(macBuf), peers[i].mac);
            EspNowMidiLog::i("  [%d]: %s", i, macBuf);
        }

        if (isEmpty()) {
            EspNowMidiLog::i("  No peers stored");
        }
    }

private:
    static constexpr uint8_t VALID_FLAG = 0xAB;

    struct StorageFormat {
        uint8_t validFlag;
        uint8_t peerCount;
        Peer peers[MAX_PEERS];
    };

    Peer peers[MAX_PEERS];
    uint8_t peerCount;
    uint16_t eepromAddr;
    bool initialized;
#if defined(ESP_PLATFORM) && !defined(ARDUINO)
    nvs_handle_t nvsHandle;
    char nvsKey[16];
#endif

    void load() {
        StorageFormat storage;

#ifdef ARDUINO
        EEPROM.get(eepromAddr, storage);
        bool found = true;
#elif defined(ESP_PLATFORM)
        size_t length = sizeof(storage);
        esp_err_t err = nvs_get_blob(nvsHandle, nvsKey, &storage, &length);
        bool found = (err == ESP_OK && length == sizeof(storage));
#endif

        if (!found || storage.validFlag != VALID_FLAG) {
            // First time use - initialize
            EspNowMidiLog::i("PeerStorage: Initializing fresh storage");
            peerCount = 0;
            memset(peers, 0, sizeof(peers));
            save();
        } else {
            // Load existing peers
            peerCount = storage.peerCount;
            if (peerCount > MAX_PEERS) {
                EspNowMidiLog::w("PeerStorage: Corrupt data, resetting");
                peerCount = 0;
                save();
            } else {
                memcpy(peers, storage.peers, sizeof(peers));
            }
        }
    }

    void save() {
        StorageFormat storage;
        storage.validFlag = VALID_FLAG;
        storage.peerCount = peerCount;
        memcpy(storage.peers, peers, sizeof(peers));

#ifdef ARDUINO
        EEPROM.put(eepromAddr, storage);
        EEPROM.commit();
#elif defined(ESP_PLATFORM)
        esp_err_t err = nvs_set_blob(nvsHandle, nvsKey, &storage, sizeof(storage));
        if (err != ESP_OK) {
            EspNowMidiLog::e("PeerStorage: nvs_set_blob failed: %d", static_cast<int>(err));
            return;
        }
        err = nvs_commit(nvsHandle);
        if (err != ESP_OK) {
            EspNowMidiLog::e("PeerStorage: nvs_commit failed: %d", static_cast<int>(err));
        }
#endif
    }

    int findIndex(const uint8_t mac[MAC_ADDRESS_SIZE]) const {
        for (int i = 0; i < peerCount; i++) {
            if (peers[i] == mac) {
                return i;
            }
        }
        return -1;
    }
};

} // namespace enomik
