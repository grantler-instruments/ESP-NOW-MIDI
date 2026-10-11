#pragma once
// Arduino GPIO / Preferences subset so enomik::Client compiles on the host
// (native tests only). Pin writes are recorded per pin.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline std::map<int, int> &stubPinLevels()
{
    static std::map<int, int> levels;
    return levels;
}
inline void pinMode(uint8_t, int) {}
inline void digitalWrite(uint8_t pin, int value) { stubPinLevels()[pin] = value; }
inline int digitalRead(uint8_t pin) { return stubPinLevels()[pin]; }
inline void analogWrite(uint8_t pin, int value) { stubPinLevels()[pin] = value; }
inline std::map<int, int> &stubAnalogLevels()
{
    static std::map<int, int> levels;
    return levels;
}
inline int analogRead(uint8_t pin) { return stubAnalogLevels()[pin]; }
inline void analogReadResolution(int) {}
inline long map(long x, long inMin, long inMax, long outMin, long outMax)
{
    return inMax == inMin ? outMin : (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}
template <typename T>
T constrain(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Byte blobs survive across Preferences instances (a "reboot") only while
// stubPrefsPersist() is true; other tests keep the empty-NVS default.
inline bool &stubPrefsPersist()
{
    static bool persist = false;
    return persist;
}
inline std::map<std::string, std::string> &stubPrefsBlobs()
{
    static std::map<std::string, std::string> blobs;
    return blobs;
}

namespace enomik
{
class Preferences
{
public:
    bool begin(const char *, bool = false) { return true; }
    void end() {}
    void clear() {}
    size_t putBytes(const char *key, const void *value, size_t len)
    {
        if (stubPrefsPersist())
            stubPrefsBlobs()[key] = std::string(static_cast<const char *>(value), len);
        return len;
    }
    // Like Arduino Preferences: 0 when the stored blob is larger than maxLen.
    size_t getBytes(const char *key, void *buf, size_t maxLen)
    {
        if (!stubPrefsPersist())
            return 0;
        auto it = stubPrefsBlobs().find(key);
        if (it == stubPrefsBlobs().end() || it->second.size() > maxLen)
            return 0;
        memcpy(buf, it->second.data(), it->second.size());
        return it->second.size();
    }
    size_t putUInt(const char *, uint32_t) { return 4; }
    uint32_t getUInt(const char *, uint32_t d = 0) { return d; }
    size_t putUChar(const char *, uint8_t) { return 1; }
    uint8_t getUChar(const char *, uint8_t d = 0) { return d; }
    size_t putBool(const char *, bool) { return 1; }
    bool getBool(const char *, bool d = false) { return d; }
};
} // namespace enomik

inline int touchRead(uint8_t) { return 0; }
inline void touchAttachInterrupt(uint8_t, void (*)(), int) {}
#define ESP_MAC_WIFI_STA 0
inline int esp_read_mac(uint8_t *mac, int)
{
    memset(mac, 0, 6);
    return 0;
}
