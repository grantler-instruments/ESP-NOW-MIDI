#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "esp_mac.h"
#endif

#include "../include/esp_now_midi_log.h"
#include "../include/esp_now_midi_compat.h"
#include <cctype>
#include <cstring>

#define MAC_ADDRESS_SIZE 6

namespace enomik {

// Convert MAC address to string (uppercase with colons)
inline PortableString macToString(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    char buf[EspNowMidiLog::MAC_STR_LEN];
    EspNowMidiLog::formatMac(buf, sizeof(buf), mac);
    return PortableString(buf);
}

// Parse MAC address from string format "XX:XX:XX:XX:XX:XX"
inline bool macFromString(const PortableString& macStr, uint8_t mac[MAC_ADDRESS_SIZE]) {
    const char* raw = macStr.c_str();
    while (*raw != '\0' && isspace(static_cast<unsigned char>(*raw))) {
        raw++;
    }

    char trimmed[18] = {0};
    size_t len = 0;
    while (raw[len] != '\0' && !isspace(static_cast<unsigned char>(raw[len])) && len < sizeof(trimmed) - 1) {
        trimmed[len] = static_cast<char>(toupper(static_cast<unsigned char>(raw[len])));
        len++;
    }

    if (len != 17) {
        EspNowMidiLog::w("MacHelpers: Invalid MAC length. Expected XX:XX:XX:XX:XX:XX");
        return false;
    }

    static const int bytePositions[MAC_ADDRESS_SIZE] = {0, 3, 6, 9, 12, 15};

    for (int i = 0; i < MAC_ADDRESS_SIZE; i++) {
        int pos = bytePositions[i];
        char char1 = trimmed[pos];
        char char2 = trimmed[pos + 1];

        auto isHex = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        };

        auto hexToNibble = [](char c) {
            return (c >= '0' && c <= '9') ? (c - '0') : (c - 'A' + 10);
        };

        if (!isHex(char1) || !isHex(char2)) {
            EspNowMidiLog::w("MacHelpers: Invalid hex at position %d", pos);
            return false;
        }

        uint8_t nibble1 = hexToNibble(char1);
        uint8_t nibble2 = hexToNibble(char2);
        mac[i] = (nibble1 << 4) | nibble2;

        // Check for colon separator (except after last byte)
        if (i < MAC_ADDRESS_SIZE - 1 && trimmed[pos + 2] != ':') {
            EspNowMidiLog::w("MacHelpers: Missing colon after byte %d", i);
            return false;
        }
    }

    return true;
}

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

// Log MAC address (no prefix)
inline void macPrint(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    EspNowMidiLog::mac("", mac);
}

// Log MAC address (alias of macPrint)
inline void macPrintln(const uint8_t mac[MAC_ADDRESS_SIZE]) {
    EspNowMidiLog::mac("", mac);
}

} // namespace enomik
