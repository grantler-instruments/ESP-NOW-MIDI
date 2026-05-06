#pragma once

#include <cstdint>
#include <cstring>
#include "utils/log.h"

#ifdef ARDUINO
  #include "EEPROM.h"
#else
  #include "nvs_flash.h"
  #include "nvs.h"
#endif

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

#ifdef ARDUINO
    PeerStorage(uint16_t eepromStartAddr = 0)
        : peerCount(0)
        , eepromAddr(eepromStartAddr)
        , initialized(false)
    {
        memset(peers, 0, sizeof(peers));
    }
#else
    PeerStorage()
        : peerCount(0)
        , nvsHandle(0)
        , initialized(false)
    {
        memset(peers, 0, sizeof(peers));
    }

    // Initialize the NVS flash partition. Must be called once at startup,
    // before begin(). Handles the erase-and-reinit case automatically.
    static bool initNVS() {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            enomik_log_debug("PeerStorage: NVS partition truncated or version changed, erasing");
            if (nvs_flash_erase() != ESP_OK) {
                enomik_log_error("PeerStorage: Failed to erase NVS flash");
                return false;
            }
            ret = nvs_flash_init();
        }
        if (ret != ESP_OK) {
            enomik_log_error("PeerStorage: Failed to initialize NVS flash");
            return false;
        }
        return true;
    }
#endif

#ifdef ARDUINO
    // Initialize EEPROM and load stored peers
    bool begin(size_t eepromSize = 512) {
        if (initialized) {
            return true;
        }

        if (!EEPROM.begin(eepromSize)) {
            enomik_log_error("PeerStorage: Failed to initialize EEPROM");
            return false;
        }
#else
    // Open the NVS namespace and load stored peers.
    // Call initNVS() once at startup before calling begin().
    bool begin(const char* ns = "peer_storage") {
        if (initialized) {
            return true;
        }

        esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvsHandle);
        if (err != ESP_OK) {
            enomik_log_error("PeerStorage: Failed to open NVS namespace");
            return false;
        }
#endif

        load();
        initialized = true;

        enomik_log_debug("PeerStorage: Loaded %d peers", peerCount);

        return true;
    }

    // Peer management
    bool add(const uint8_t mac[MAC_ADDRESS_SIZE]) {
        if (!initialized) {
            enomik_log_error("PeerStorage: Not initialized");
            return false;
        }
        
        if (isFull()) {
            enomik_log_error("PeerStorage: Maximum peers reached");
            return false;
        }
        
        if (exists(mac)) {
            enomik_log_debug("PeerStorage: Peer already exists");
            return false;
        }
        
        memcpy(peers[peerCount].mac, mac, MAC_ADDRESS_SIZE);
        peerCount++;
        save();
        
        enomik_log_debug("PeerStorage: Added peer %02X:%02X:%02X:%02X:%02X:%02X (Total: %d)",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], peerCount);
        
        return true;
    }
    
    bool remove(const uint8_t mac[MAC_ADDRESS_SIZE]) {
        int index = findIndex(mac);
        if (index < 0) {
            enomik_log_error("PeerStorage: Peer not found");
            return false;
        }
        
        return remove(index);
    }
    
    bool remove(int index) {
        if (index < 0 || index >= peerCount) {
            enomik_log_error("PeerStorage: Invalid index");
            return false;
        }
        
        enomik_log_debug("PeerStorage: Removing peer %02X:%02X:%02X:%02X:%02X:%02X",
            peers[index].mac[0], peers[index].mac[1], peers[index].mac[2],
            peers[index].mac[3], peers[index].mac[4], peers[index].mac[5]);
        
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
        enomik_log_debug("PeerStorage: All peers cleared");
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
        enomik_log("PeerStorage: Stored peers (%d):", peerCount);
        for (int i = 0; i < peerCount; i++) {
            enomik_log("  [%d]: %02X:%02X:%02X:%02X:%02X:%02X", i,
                peers[i].mac[0], peers[i].mac[1], peers[i].mac[2],
                peers[i].mac[3], peers[i].mac[4], peers[i].mac[5]);
        }
        if (isEmpty()) {
            enomik_log("  No peers stored");
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
    bool initialized;

#ifdef ARDUINO
    uint16_t eepromAddr;
#else
    nvs_handle_t nvsHandle;
#endif

    void load() {
        StorageFormat storage;

#ifdef ARDUINO
        EEPROM.get(eepromAddr, storage);

        if (storage.validFlag != VALID_FLAG) {
            enomik_log_debug("PeerStorage: Initializing fresh storage");
            peerCount = 0;
            memset(peers, 0, sizeof(peers));
            save();
            return;
        }
#else
        size_t size = sizeof(StorageFormat);
        esp_err_t err = nvs_get_blob(nvsHandle, "peers", &storage, &size);

        if (err == ESP_ERR_NVS_NOT_FOUND || storage.validFlag != VALID_FLAG) {
            enomik_log_debug("PeerStorage: Initializing fresh storage");
            peerCount = 0;
            memset(peers, 0, sizeof(peers));
            save();
            return;
        }
#endif

        peerCount = storage.peerCount;
        if (peerCount > MAX_PEERS) {
            enomik_log_error("PeerStorage: Corrupt data, resetting");
            peerCount = 0;
            save();
        } else {
            memcpy(peers, storage.peers, sizeof(peers));
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
#else
        nvs_set_blob(nvsHandle, "peers", &storage, sizeof(StorageFormat));
        nvs_commit(nvsHandle);
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
