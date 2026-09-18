/**
 * @file esp_now_midi_sysex.h
 * @brief Fragmented SysEx framing and reassembly for ESP-NOW MIDI.
 *
 * Short MIDI stays 1–3 raw bytes. Packets with len > 3 must match this frame:
 *
 *   [0]     marker   = 0xF0
 *   [1]     flags    = bit0 FIRST | bit1 LAST | bits4-7 version=1
 *   [2]     seq      = 0-based fragment index
 *   [3]     total    = fragment count (>=1)
 *   [4..5]  msg_len  = full message length, little-endian
 *   [6..]   payload  = 1..240 bytes
 */
#pragma once

#include <cstdint>
#include <cstring>

namespace esp_now_midi_sysex
{

constexpr uint8_t MARKER = 0xF0;
constexpr uint8_t VERSION = 1;
constexpr uint8_t VERSION_SHIFT = 4;
constexpr uint8_t FLAG_FIRST = 0x01;
constexpr uint8_t FLAG_LAST = 0x02;
constexpr uint8_t HEADER_SIZE = 6;
constexpr uint16_t MAX_PAYLOAD = 240;
constexpr uint16_t MAX_MESSAGE = 1024;
constexpr uint16_t MAX_FRAME = HEADER_SIZE + MAX_PAYLOAD; // 246
constexpr uint8_t MAX_REASSEMBLY_SLOTS = 4;
constexpr uint32_t REASSEMBLY_TIMEOUT_MS = 100;

inline uint8_t makeFlags(bool first, bool last)
{
    uint8_t flags = static_cast<uint8_t>(VERSION << VERSION_SHIFT);
    if (first)
        flags |= FLAG_FIRST;
    if (last)
        flags |= FLAG_LAST;
    return flags;
}

inline uint8_t flagsVersion(uint8_t flags)
{
    return static_cast<uint8_t>((flags >> VERSION_SHIFT) & 0x0F);
}

inline uint8_t fragmentCount(uint16_t msgLen)
{
    if (msgLen == 0)
        return 0;
    return static_cast<uint8_t>((msgLen + MAX_PAYLOAD - 1) / MAX_PAYLOAD);
}

inline uint16_t fragmentPayloadOffset(uint8_t seq)
{
    return static_cast<uint16_t>(seq) * MAX_PAYLOAD;
}

inline uint16_t fragmentPayloadLength(uint16_t msgLen, uint8_t seq)
{
    const uint16_t offset = fragmentPayloadOffset(seq);
    if (offset >= msgLen)
        return 0;
    const uint16_t remaining = static_cast<uint16_t>(msgLen - offset);
    return remaining > MAX_PAYLOAD ? MAX_PAYLOAD : remaining;
}

/** Parsed view of one on-air SysEx fragment. */
struct FrameView
{
    uint8_t flags;
    uint8_t seq;
    uint8_t total;
    uint16_t msgLen;
    const uint8_t *payload;
    uint16_t payloadLen;
};

/**
 * @brief Validates and parses one SysEx transport frame.
 * @return true when the frame is well-formed.
 */
inline bool parseFrame(const uint8_t *data, int len, FrameView &out)
{
    if (data == nullptr || len < HEADER_SIZE + 1 || len > static_cast<int>(MAX_FRAME))
        return false;

    if (data[0] != MARKER)
        return false;

    const uint8_t flags = data[1];
    if (flagsVersion(flags) != VERSION)
        return false;

    const uint8_t seq = data[2];
    const uint8_t total = data[3];
    const uint16_t msgLen = static_cast<uint16_t>(data[4] | (static_cast<uint16_t>(data[5]) << 8));
    const uint16_t payloadLen = static_cast<uint16_t>(len - HEADER_SIZE);

    if (total < 1 || seq >= total)
        return false;
    if (msgLen == 0 || msgLen > MAX_MESSAGE)
        return false;
    if (fragmentCount(msgLen) != total)
        return false;

    const bool first = (flags & FLAG_FIRST) != 0;
    const bool last = (flags & FLAG_LAST) != 0;
    if (first != (seq == 0))
        return false;
    if (last != (seq == total - 1))
        return false;

    const uint16_t expected = fragmentPayloadLength(msgLen, seq);
    if (payloadLen == 0 || payloadLen != expected)
        return false;

    out.flags = flags;
    out.seq = seq;
    out.total = total;
    out.msgLen = msgLen;
    out.payload = data + HEADER_SIZE;
    out.payloadLen = payloadLen;
    return true;
}

/**
 * @brief Encodes one fragment into @p out.
 * @return Encoded byte count, or 0 on error.
 */
inline size_t encodeFrame(uint8_t *out, size_t outCap, uint8_t seq, uint8_t total,
                          uint16_t msgLen, const uint8_t *payload, uint16_t payloadLen)
{
    if (out == nullptr || payload == nullptr || payloadLen == 0 || payloadLen > MAX_PAYLOAD)
        return 0;
    if (msgLen == 0 || msgLen > MAX_MESSAGE || total == 0 || seq >= total)
        return 0;
    if (fragmentCount(msgLen) != total)
        return 0;
    if (payloadLen != fragmentPayloadLength(msgLen, seq))
        return 0;

    const size_t frameLen = HEADER_SIZE + payloadLen;
    if (outCap < frameLen)
        return 0;

    out[0] = MARKER;
    out[1] = makeFlags(seq == 0, seq == total - 1);
    out[2] = seq;
    out[3] = total;
    out[4] = static_cast<uint8_t>(msgLen & 0xFF);
    out[5] = static_cast<uint8_t>((msgLen >> 8) & 0xFF);
    memcpy(out + HEADER_SIZE, payload, payloadLen);
    return frameLen;
}

/**
 * @brief Per-MAC SysEx reassembly with fixed slots (no heap).
 *
 * Pass @p nowMs into feed() so callers/tests control timeouts.
 * On completion, outData points into an internal slot valid until the next
 * feed() that touches that slot.
 */
class Reassembler
{
public:
    Reassembler()
    {
        clear();
    }

    void clear()
    {
        for (uint8_t i = 0; i < MAX_REASSEMBLY_SLOTS; ++i)
            _slots[i].active = false;
    }

    /**
     * @brief Feed one received ESP-NOW payload.
     * @return true when a complete SysEx message is ready in outData/outLen.
     */
    bool feed(const uint8_t mac[6], const uint8_t *data, int len, uint32_t nowMs,
              const uint8_t *&outData, uint16_t &outLen)
    {
        outData = nullptr;
        outLen = 0;

        FrameView frame;
        if (!parseFrame(data, len, frame))
            return false;

        expireStale(nowMs);

        Slot *slot = findSlot(mac);
        if (frame.seq == 0)
        {
            if (slot == nullptr)
                slot = allocSlot(mac, nowMs);
            if (slot == nullptr)
                return false;
            startAssembly(*slot, mac, frame, nowMs);
        }
        else
        {
            if (slot == nullptr || !slot->active)
                return false;
            if (slot->msgLen != frame.msgLen || slot->total != frame.total)
                return false;
            if (frame.seq != slot->expectedSeq)
                return false;
        }

        const uint16_t offset = fragmentPayloadOffset(frame.seq);
        if (static_cast<uint32_t>(offset) + frame.payloadLen > slot->msgLen)
            return false;

        memcpy(slot->buffer + offset, frame.payload, frame.payloadLen);
        slot->expectedSeq = static_cast<uint8_t>(frame.seq + 1);
        slot->lastMs = nowMs;

        if ((frame.flags & FLAG_LAST) == 0)
            return false;

        if (slot->expectedSeq != slot->total)
            return false;

        outData = slot->buffer;
        outLen = slot->msgLen;
        slot->active = false;
        return true;
    }

private:
    struct Slot
    {
        bool active;
        uint8_t mac[6];
        uint16_t msgLen;
        uint8_t total;
        uint8_t expectedSeq;
        uint32_t lastMs;
        uint8_t buffer[MAX_MESSAGE];
    };

    Slot _slots[MAX_REASSEMBLY_SLOTS];

    static bool macEqual(const uint8_t a[6], const uint8_t b[6])
    {
        return memcmp(a, b, 6) == 0;
    }

    Slot *findSlot(const uint8_t mac[6])
    {
        for (uint8_t i = 0; i < MAX_REASSEMBLY_SLOTS; ++i)
        {
            if (_slots[i].active && macEqual(_slots[i].mac, mac))
                return &_slots[i];
        }
        return nullptr;
    }

    Slot *allocSlot(const uint8_t mac[6], uint32_t nowMs)
    {
        for (uint8_t i = 0; i < MAX_REASSEMBLY_SLOTS; ++i)
        {
            if (!_slots[i].active)
                return &_slots[i];
        }

        // Reuse least-recently-updated slot.
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < MAX_REASSEMBLY_SLOTS; ++i)
        {
            if (static_cast<int32_t>(nowMs - _slots[i].lastMs) >
                static_cast<int32_t>(nowMs - _slots[oldest].lastMs))
            {
                oldest = i;
            }
        }
        (void)mac;
        return &_slots[oldest];
    }

    void startAssembly(Slot &slot, const uint8_t mac[6], const FrameView &frame, uint32_t nowMs)
    {
        slot.active = true;
        memcpy(slot.mac, mac, 6);
        slot.msgLen = frame.msgLen;
        slot.total = frame.total;
        slot.expectedSeq = 0;
        slot.lastMs = nowMs;
    }

    void expireStale(uint32_t nowMs)
    {
        for (uint8_t i = 0; i < MAX_REASSEMBLY_SLOTS; ++i)
        {
            if (!_slots[i].active)
                continue;
            if (static_cast<uint32_t>(nowMs - _slots[i].lastMs) > REASSEMBLY_TIMEOUT_MS)
                _slots[i].active = false;
        }
    }
};

} // namespace esp_now_midi_sysex
