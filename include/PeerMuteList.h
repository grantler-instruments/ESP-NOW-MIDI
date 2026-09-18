#pragma once

#include <cstdint>
#include <cstring>

#ifndef MAX_PEERS
#define MAX_PEERS 20
#endif

namespace enomik
{

/**
 * @brief Session-only set of muted peer MACs.
 *
 * Used by `enomik::Dongle` to filter a peer in both directions. Not persisted.
 */
class PeerMuteList
{
public:
    bool set(const uint8_t mac[6], bool muted)
    {
        if (!mac)
        {
            return false;
        }
        const uint64_t packed = packMac(mac);
        const int index = find(packed);
        if (muted)
        {
            if (index >= 0)
            {
                return true;
            }
            if (count_ >= MAX_PEERS)
            {
                return false;
            }
            packed_[count_++] = packed;
            return true;
        }
        if (index >= 0)
        {
            packed_[index] = packed_[count_ - 1];
            count_--;
        }
        return true;
    }

    bool contains(const uint8_t mac[6]) const
    {
        if (!mac)
        {
            return false;
        }
        return find(packMac(mac)) >= 0;
    }

    void clear(const uint8_t mac[6])
    {
        if (!mac)
        {
            return;
        }
        const int index = find(packMac(mac));
        if (index < 0)
        {
            return;
        }
        packed_[index] = packed_[count_ - 1];
        count_--;
    }

    int count() const { return count_; }

private:
    static uint64_t packMac(const uint8_t mac[6])
    {
        uint64_t packed = 0;
        for (int i = 0; i < 6; i++)
        {
            packed |= (static_cast<uint64_t>(mac[i]) << (i * 8));
        }
        return packed;
    }

    int find(uint64_t packed) const
    {
        for (int i = 0; i < count_; i++)
        {
            if (packed_[i] == packed)
            {
                return i;
            }
        }
        return -1;
    }

    uint64_t packed_[MAX_PEERS] = {};
    int count_ = 0;
};

} // namespace enomik
