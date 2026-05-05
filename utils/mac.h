#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include "esp_mac.h"
#include "../utils/log.h"

#define MAC_ADDRESS_SIZE 6

namespace enomik {

// Returns pointer to a static buffer — single-threaded use only
inline const char* macToString(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    static char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

// Parse MAC address from C string "XX:XX:XX:XX:XX:XX"
inline bool macFromString(const char* macStr, uint8_t mac[MAC_ADDRESS_SIZE]) {
    if (!macStr || strlen(macStr) != 17) {
        enomik_log_error("MacHelpers: Invalid MAC length. Expected XX:XX:XX:XX:XX:XX");
        return false;
    }

    // Normalize to uppercase in a local copy
    char upper[18];
    for (int i = 0; i < 17; i++) {
        char c = macStr[i];
        upper[i] = (c >= 'a' && c <= 'f') ? (c - 'a' + 'A') : c;
    }
    upper[17] = '\0';

    int bytePositions[MAC_ADDRESS_SIZE] = {0, 3, 6, 9, 12, 15};

    for (int i = 0; i < MAC_ADDRESS_SIZE; i++) {
        int pos = bytePositions[i];
        char char1 = upper[pos];
        char char2 = upper[pos + 1];

        auto isHex = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        };
        auto hexToNibble = [](char c) -> uint8_t {
            return (c >= '0' && c <= '9') ? (c - '0') : (c - 'A' + 10);
        };

        if (!isHex(char1) || !isHex(char2)) {
            enomik_log_error("MacHelpers: Invalid hex at position %d", pos);
            return false;
        }

        mac[i] = (hexToNibble(char1) << 4) | hexToNibble(char2);

        if (i < MAC_ADDRESS_SIZE - 1 && upper[pos + 2] != ':') {
            enomik_log_error("MacHelpers: Missing colon after byte %d", i);
            return false;
        }
    }

    return true;
}

#ifdef ARDUINO
#include <Arduino.h>
// Convenience overload for Arduino String callers
inline bool macFromString(const String& macStr, uint8_t mac[MAC_ADDRESS_SIZE]) {
    return macFromString(macStr.c_str(), mac);
}
#endif

// Compare two MAC addresses
inline bool macEquals(const uint8_t mac1[MAC_ADDRESS_SIZE], const uint8_t mac2[MAC_ADDRESS_SIZE]) {
    return memcmp(mac1, mac2, MAC_ADDRESS_SIZE) == 0;
}

// Copy MAC address
inline void macCopy(uint8_t dest[MAC_ADDRESS_SIZE], const uint8_t src[MAC_ADDRESS_SIZE]) {
    memcpy(dest, src, MAC_ADDRESS_SIZE);
}

// Check if MAC is all zeros
inline bool macIsZero(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    for (int i = 0; i < MAC_ADDRESS_SIZE; i++) {
        if (mac[i] != 0) return false;
    }
    return true;
}

// Check if MAC is broadcast address (all 0xFF)
inline bool macIsBroadcast(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    for (int i = 0; i < MAC_ADDRESS_SIZE; i++) {
        if (mac[i] != 0xFF) return false;
    }
    return true;
}

// Print MAC address via logger
inline void macPrint(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    enomik_log("%s", macToString(mac));
}

// Alias kept for API compatibility
inline void macPrintln(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    macPrint(mac);
}

} // namespace enomik
