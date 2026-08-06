#pragma once

#include "./esp_now_midi_helpers.h"
#include "./midiTimedPacket.h"
#include <cstdint>
#include <cstring>

#ifndef MAX_PEERS
#define MAX_PEERS 20
#endif

/**
 * @brief Per-peer timed MIDI playout ring (no Arduino / ESP-NOW deps).
 *
 * Call push() from the receive path and popDue() / forcePopOldest() from update().
 * Time is injected as microseconds so native tests can drive the clock.
 */
class MidiJitterBuffer
{
public:
  struct Entry
  {
    uint32_t playout_us = 0;
    midi_message message{};
  };

  struct PeerState
  {
    uint64_t packed_mac = 0;
    bool in_use = false;
    bool anchored = false;
    uint32_t t0_local_us = 0;
    uint16_t t0_sender_tick = 0;
    uint32_t last_rx_us = 0;
    Entry entries[ESP_NOW_MIDI_JITTER_BUFFER_SIZE];
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;
  };

  static uint64_t packMac(const uint8_t mac[6])
  {
    uint64_t packed = 0;
    for (int i = 0; i < 6; i++)
    {
      packed |= (static_cast<uint64_t>(mac[i]) << (i * 8));
    }
    return packed;
  }

  void clear()
  {
    for (auto &peer : _peers)
    {
      peer = PeerState{};
    }
  }

  PeerState *findPeer(uint64_t packedMac)
  {
    for (auto &peer : _peers)
    {
      if (peer.in_use && peer.packed_mac == packedMac)
      {
        return &peer;
      }
    }
    return nullptr;
  }

  PeerState *findOrAllocPeer(uint64_t packedMac)
  {
    if (PeerState *existing = findPeer(packedMac))
    {
      return existing;
    }
    for (auto &peer : _peers)
    {
      if (!peer.in_use)
      {
        peer = PeerState{};
        peer.in_use = true;
        peer.packed_mac = packedMac;
        return &peer;
      }
    }
    return nullptr;
  }

  static bool isFull(const PeerState &peer)
  {
    return peer.count >= ESP_NOW_MIDI_JITTER_BUFFER_SIZE;
  }

  static bool forcePopOldest(PeerState &peer, Entry &out)
  {
    if (peer.count == 0)
    {
      return false;
    }
    out = peer.entries[peer.tail];
    peer.tail = static_cast<uint8_t>((peer.tail + 1) % ESP_NOW_MIDI_JITTER_BUFFER_SIZE);
    --peer.count;
    return true;
  }

  static bool peek(const PeerState &peer, Entry &out)
  {
    if (peer.count == 0)
    {
      return false;
    }
    out = peer.entries[peer.tail];
    return true;
  }

  static bool pop(PeerState &peer, Entry &out)
  {
    if (!forcePopOldest(peer, out))
    {
      return false;
    }
    return true;
  }

  /**
   * @brief Computes playout time, re-anchoring when needed, and enqueues.
   * @param forcedOut Set when the ring was full and the oldest entry was evicted
   * for immediate dispatch (never drops).
   * @return `true` when @p forcedOut is valid.
   */
  bool push(PeerState &peer, uint32_t now_us, uint16_t sender_tick, const midi_message &msg,
            uint32_t t_us, uint32_t reanchor_gap_us, uint32_t absurd_offset_us, Entry &forcedOut,
            bool &hasForced)
  {
    hasForced = false;
    maybeReanchor(peer, now_us, sender_tick, t_us, reanchor_gap_us, absurd_offset_us);

    const uint16_t tickDelta = static_cast<uint16_t>(sender_tick - peer.t0_sender_tick);
    uint32_t playout = peer.t0_local_us + midi_timed_packet::tickDeltaToMicros(tickDelta) + t_us;

    // Late relative to playout → release ASAP (do not accumulate lag).
    if (static_cast<int32_t>(playout - now_us) < 0)
    {
      playout = now_us;
    }

    if (isFull(peer))
    {
      hasForced = forcePopOldest(peer, forcedOut);
    }

    peer.entries[peer.head].playout_us = playout;
    peer.entries[peer.head].message = msg;
    peer.head = static_cast<uint8_t>((peer.head + 1) % ESP_NOW_MIDI_JITTER_BUFFER_SIZE);
    ++peer.count;
    peer.last_rx_us = now_us;
    return hasForced;
  }

  /**
   * @brief Pops the next entry if it is due at @p now_us.
   */
  static bool popDue(PeerState &peer, uint32_t now_us, Entry &out)
  {
    Entry front;
    if (!peek(peer, front))
    {
      return false;
    }
    if (static_cast<int32_t>(front.playout_us - now_us) > 0)
    {
      return false;
    }
    return pop(peer, out);
  }

  PeerState *peersBegin() { return _peers; }
  PeerState *peersEnd() { return _peers + kMaxPeers; }
  static constexpr int kMaxPeers = MAX_PEERS;

private:
  PeerState _peers[MAX_PEERS];

  static void anchor(PeerState &peer, uint32_t now_us, uint16_t sender_tick)
  {
    peer.anchored = true;
    peer.t0_local_us = now_us;
    peer.t0_sender_tick = sender_tick;
    peer.last_rx_us = now_us;
  }

  static void maybeReanchor(PeerState &peer, uint32_t now_us, uint16_t sender_tick, uint32_t t_us,
                            uint32_t reanchor_gap_us, uint32_t absurd_offset_us)
  {
    if (!peer.anchored)
    {
      anchor(peer, now_us, sender_tick);
      return;
    }

    const uint32_t gap = now_us - peer.last_rx_us;
    if (gap > reanchor_gap_us)
    {
      anchor(peer, now_us, sender_tick);
      return;
    }

    const uint16_t tickDelta = static_cast<uint16_t>(sender_tick - peer.t0_sender_tick);
    const uint32_t playout =
        peer.t0_local_us + midi_timed_packet::tickDeltaToMicros(tickDelta) + t_us;
    const int32_t wait = static_cast<int32_t>(playout - now_us);
    if (wait > static_cast<int32_t>(absurd_offset_us))
    {
      anchor(peer, now_us, sender_tick);
    }
  }
};
