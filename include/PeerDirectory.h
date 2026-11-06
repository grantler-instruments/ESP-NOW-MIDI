#pragma once

#include <cstdint>
#include <cstring>

#include "./esp_now_midi_helpers.h"
#include "./esp_now_midi_lock.h"
#include "./enomik_sysex_codec.h"

namespace enomik
{

/**
 * @brief What peers told the dongle about themselves (SysEx ANNOUNCE).
 *
 * Written from the ESP-NOW receive callback, read from the loop task, so every
 * access is locked. Not persisted: filled again by the peers' next announce.
 */
class PeerDirectory
{
public:
    /** Stores @p info for @p mac. @return `true` when it is new or changed. */
    bool set(const uint8_t mac[6], const Announce &info)
    {
        if (!mac)
        {
            return false;
        }
        const uint64_t packed = pack(mac);
        EspNowMidiLock lock(_mux);
        int index = find(packed);
        if (index < 0)
        {
            if (_count >= MAX_PEERS)
            {
                return false;
            }
            index = _count++;
            _entries[index].packed = packed;
        }
        else if (sameInfo(_entries[index].info, info))
        {
            return false;
        }
        _entries[index].info = info;
        return true;
    }

    bool get(const uint8_t mac[6], Announce &out) const
    {
        if (!mac)
        {
            return false;
        }
        EspNowMidiLock lock(_mux);
        const int index = find(pack(mac));
        if (index < 0)
        {
            return false;
        }
        out = _entries[index].info;
        return true;
    }

    void remove(const uint8_t mac[6])
    {
        if (!mac)
        {
            return;
        }
        EspNowMidiLock lock(_mux);
        const int index = find(pack(mac));
        if (index >= 0)
        {
            _entries[index] = _entries[--_count];
        }
    }

    void clear()
    {
        EspNowMidiLock lock(_mux);
        _count = 0;
    }

private:
    struct Entry
    {
        uint64_t packed;
        Announce info;
    };

    static uint64_t pack(const uint8_t mac[6])
    {
        uint64_t packed = 0;
        for (int i = 0; i < 6; i++)
        {
            packed |= static_cast<uint64_t>(mac[i]) << (i * 8);
        }
        return packed;
    }

    static bool sameInfo(const Announce &a, const Announce &b)
    {
        return a.kind == b.kind && a.major == b.major && a.minor == b.minor &&
               a.patch == b.patch && strcmp(a.name, b.name) == 0;
    }

    int find(uint64_t packed) const
    {
        for (int i = 0; i < _count; i++)
        {
            if (_entries[i].packed == packed)
            {
                return i;
            }
        }
        return -1;
    }

    Entry _entries[MAX_PEERS];
    int _count = 0;
    mutable portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

} // namespace enomik
