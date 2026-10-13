/**
 * @file esp_now_midi.h
 * @brief ESP-NOW transport and MIDI message API.
 */
#pragma once
#ifndef ESP_NOW_MIDI_CHANNEL
/** Wi-Fi channel used by ESP-NOW MIDI peers. Override before including this header. */
#define ESP_NOW_MIDI_CHANNEL 6
#endif
#include "./version.h"
#include <cstdint>
#include <cstring>
#include <esp_now.h>
#include <esp_wifi.h>
#include "./esp_now_midi_helpers.h"
#include "./esp_now_midi_sysex.h"
#include "./esp_now_midi_lock.h"
#include "./esp_now_midi_log.h"
#include "./esp_now_midi_wifi.h"
#include "./esp_now_midi_send_queue.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include "freertos/task.h"
#define ESP_NOW_MIDI_HAS_TASKS 1
#else
#define ESP_NOW_MIDI_HAS_TASKS 0 // host tests: the queue is sent from service()
#endif

#ifndef ARDUINO
using byte = uint8_t;
#endif

/** @brief Stored representation of an ESP-NOW peer. */
struct PeerInfo
{
  uint8_t mac[6];       ///< Peer Wi-Fi MAC address.
  uint64_t packed_mac; ///< MAC address packed into the lower 48 bits.
  uint32_t lastSeenMs; ///< millis() of the last valid packet from this peer.
  bool seen;           ///< A valid packet arrived since the peer was added.
  uint8_t missed;      ///< Sends in a row the peer did not acknowledge.
  bool unreachable;    ///< Skipped by sends, except one probe per ESP_NOW_MIDI_PROBE_INTERVAL_MS.
  uint32_t nextProbeMs; ///< millis() of the next probe while unreachable.

  /**
   * @brief Packs a six-byte MAC address for fast comparisons.
   * @param mac MAC address to pack.
   * @return The packed MAC address.
   */
  static uint64_t packMac(const uint8_t mac[6])
  {
    uint64_t packed = 0;
    for (int i = 0; i < 6; i++)
    {
      packed |= ((uint64_t)mac[i] << (i * 8));
    }
    return packed;
  }
};

/** @brief ESP-NOW send counters since boot (see esp_now_midi::getSendStats()). */
struct EspNowSendStats
{
  uint32_t sent = 0;         ///< Packets ESP-NOW accepted for sending.
  uint32_t failed = 0;       ///< Sends ESP-NOW refused, or sends with no peers.
  uint32_t delivered = 0;    ///< Packets the receiver acknowledged.
  uint32_t notDelivered = 0; ///< Packets sent but not acknowledged.
  esp_err_t lastError = ESP_OK; ///< Last refusal (`ESP_FAIL`: no peers).
  uint32_t throttled = 0;    ///< Sends dropped after waiting for a pending confirmation (also in `failed`).
  uint32_t restarts = 0;     ///< ESP-NOW restarts after the driver stopped confirming sends.
  uint32_t wifiRestarts = 0; ///< Of those, restarts that also stopped and started Wi-Fi.
  uint32_t skipped = 0;      ///< Packets not sent to a peer marked unreachable (see isPeerReachable()).
  uint32_t dropped = 0;      ///< Packets the full send queue refused (also in `failed`).
  uint32_t replaced = 0;     ///< Controller values that replaced a waiting one for the same controller.
  uint32_t queuePeak = 0;    ///< Most packets waiting in the send queue at once.
};

#ifndef ESP_NOW_MIDI_SEND_QUEUE
/**
 * 1: sends only queue the packet and return; a sender task sends the queue,
 * one packet at a time. This also works from receive handlers, which run in
 * the task that reports send results. 0: every send waits for the previous
 * confirmation itself (up to ESP_NOW_MIDI_SEND_WAIT_MS, then drops).
 */
#define ESP_NOW_MIDI_SEND_QUEUE 1
#endif

#ifndef ESP_NOW_MIDI_QUEUE_SHORT
/** Send queue slots for short MIDI messages (one per message and peer). */
#define ESP_NOW_MIDI_QUEUE_SHORT 128
#endif

#ifndef ESP_NOW_MIDI_QUEUE_FRAMES
/** Send queue slots for SysEx frames (one per frame and peer). */
#define ESP_NOW_MIDI_QUEUE_FRAMES 8
#endif

#ifndef ESP_NOW_MIDI_FRAME_WAIT_MS
/** How long sendSysex() waits for a free frame slot before it gives up. */
#define ESP_NOW_MIDI_FRAME_WAIT_MS 200
#endif

#ifndef ESP_NOW_MIDI_SENDER_PRIORITY
/** FreeRTOS priority of the sender task: above loop(), below Wi-Fi and USB. */
#define ESP_NOW_MIDI_SENDER_PRIORITY 3
#endif

#ifndef ESP_NOW_MIDI_SENDER_STACK
/** Stack of the sender task, in bytes. */
#define ESP_NOW_MIDI_SENDER_STACK 3072
#endif

#ifndef ESP_NOW_MIDI_MAX_IN_FLIGHT
/**
 * Sent packets that may wait for their confirmation at once. With more than
 * one, a sustained load (hundreds of packets per second) made the ESP-NOW
 * driver stop confirming sends for good on ESP32-S2; with one it did not.
 */
#define ESP_NOW_MIDI_MAX_IN_FLIGHT 1
#endif

#ifndef ESP_NOW_MIDI_SEND_WAIT_MS
/**
 * Without the send queue: how long a send waits for a pending confirmation
 * before it is dropped. With it: how long a note off (or another High
 * priority message, see esp_now_midi_queue::classify()) waits for a free slot.
 */
#define ESP_NOW_MIDI_SEND_WAIT_MS 5
#endif

#ifndef ESP_NOW_MIDI_STALL_MS
/** Without a confirmation for this long while sending, ESP-NOW is restarted. */
#define ESP_NOW_MIDI_STALL_MS 1000
#endif

#ifndef ESP_NOW_MIDI_UNREACHABLE_AFTER
/**
 * A peer that acknowledged none of this many sends in a row is skipped,
 * except for a probe every ESP_NOW_MIDI_PROBE_INTERVAL_MS, until it
 * acknowledges or sends again. Waiting for a missing peer's "not delivered"
 * takes up to ~90 ms, which with ESP_NOW_MIDI_MAX_IN_FLIGHT = 1 held back the
 * sends to every other peer. 0 = never skip.
 */
#define ESP_NOW_MIDI_UNREACHABLE_AFTER 5
#endif

#ifndef ESP_NOW_MIDI_PROBE_INTERVAL_MS
/** How often an unreachable peer still gets a packet, to notice it is back. */
#define ESP_NOW_MIDI_PROBE_INTERVAL_MS 2000
#endif

#ifndef ESP_NOW_MIDI_RESTART_BACKOFF_MS
/** Minimum time between two ESP-NOW restarts. */
#define ESP_NOW_MIDI_RESTART_BACKOFF_MS 5000
#endif

/**
 * @brief Sends and receives MIDI messages over ESP-NOW.
 *
 * Call begin() once before registering peers or exchanging messages. All send
 * methods transmit to every registered peer. Register receive callbacks with
 * the `setHandle*` methods.
 */
class esp_now_midi
{
public:
  /**
   * @brief Callback invoked after ESP-NOW sends a packet.
   * @param info ESP-IDF transmission details (`wifi_tx_info_t`; IDF ≥ 5.5).
   * @param status Delivery result reported by ESP-NOW.
   */
  typedef void (*DataSentCallback)(const wifi_tx_info_t *info, esp_now_send_status_t status);

  /**
   * @brief Default ESP-NOW send-status callback.
   *
   * When `ESP_NOW_DEBUGGING` is `1`, logs the delivery result.
   *
   * @param info ESP-IDF transmission details.
   * @param status Delivery result reported by ESP-NOW.
   */
  static void DefaultOnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
  {
    (void)info;
    EspNowMidiLog::d("Last Packet Send Status: %s",
                     status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  }

  /**
   * @brief Internal adapter that routes ESP-NOW send events to the active instance.
   */
  static void SendCallbackAdapter(const wifi_tx_info_t *info, esp_now_send_status_t status)
  {
    if (!_instance)
    {
      return;
    }
    {
      const uint32_t now = static_cast<uint32_t>(millis());
      EspNowMidiLock lock(_instance->_statsMux);
      EspNowSendStats &stats = _instance->_stats;
      ++(status == ESP_NOW_SEND_SUCCESS ? stats.delivered : stats.notDelivered);
      _instance->noteDelivery(info && info->des_addr ? PeerInfo::packMac(info->des_addr)
                                                     : _instance->_lastSentPacked,
                              status == ESP_NOW_SEND_SUCCESS, now);
      if (_instance->_inFlight > 0)
      {
        --_instance->_inFlight;
      }
      _instance->_lastConfirmMs = now;
      _instance->_refusedSinceConfirm = false;
      _instance->_confirmedSinceRestart = true;
    }
    _instance->noteWifiTask();
    _instance->wakeSender();
    if (_instance->userDataSentCallback)
    {
      _instance->userDataSentCallback(info, status);
    }
  }

  /**
   * @brief Internal adapter that routes ESP-NOW receive events to the active instance.
   */
  static void OnDataRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len)
  {
    if (_instance)
    {
      _instance->noteWifiTask();
      _instance->OnDataRecv(recv_info->src_addr, incomingData, len);
    }
  }
  /**
   * @brief Initializes ESP-NOW (and optionally Wi-Fi STA) plus send/recv callbacks.
   *
   * Initialization failures are logged and return `false`.
   * Only one active instance is supported because ESP-NOW callbacks are routed
   * through a static instance pointer.
   *
   * @param reducePowerAtCostOfLatency Enable modem sleep and lower transmit
   * power to save energy; disabled by default for lower latency.
   * @param autoPeerDiscovery Add an unknown sender as a peer when it first
   * sends a valid MIDI packet (see isMidiPacket()).
   * @param callback Optional callback invoked after each ESP-NOW send.
   * @param manageWifi When `true` (default), bring up Wi-Fi STA via the
   * Arduino or ESP-IDF backend. When `false`, the application must already
   * have started Wi-Fi; this call only configures ESP-NOW, channel, and power.
   * Peers must share the same radio channel.
   * @return `true` when ESP-NOW is ready (including already-initialized).
   */
  bool begin(bool reducePowerAtCostOfLatency = false, bool autoPeerDiscovery = true,
             DataSentCallback callback = DefaultOnDataSent, bool manageWifi = true)
  {
    _instance = this;
    _autoPeerDiscovery = autoPeerDiscovery;
    userDataSentCallback = callback; // This needs to be INSIDE the function

    if (manageWifi)
    {
      if (!esp_now_midi_wifi::ensureWifiSta())
      {
        EspNowMidiLog::e("Wi-Fi STA bring-up failed");
        return false;
      }
    }

    // Try to initialize ESP-NOW (gracefully handle if already initialized)
    esp_err_t init_result = esp_now_init();

    if (init_result == ESP_ERR_ESPNOW_EXIST)
    {
      EspNowMidiLog::i("Already initialized");
    }
    else if (init_result != ESP_OK)
    {
      EspNowMidiLog::e("Init failed with error: %d", init_result);
      return false;
    }

    // Set channel and power
    esp_wifi_set_channel(ESP_NOW_MIDI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    setReducePowerAtCostOfLatency(reducePowerAtCostOfLatency);

    // A repeated begin() starts empty; remove the old peers from the driver
    // too, or adding them again fails with "already exists".
    if (getPeersCount() > 0)
    {
      clearPeers();
    }

    // Register callbacks
    esp_now_register_send_cb(SendCallbackAdapter);
    esp_now_register_recv_cb(OnDataRecvStatic);
    startSender();
    return true;
  }

  /**
   * @brief Enables or disables power saving at the cost of latency.
   *
   * When enabled, modem sleep is on and transmit power is lowered. Can be
   * called after begin() to toggle at runtime.
   *
   * @param enabled `true` to prefer power saving; `false` for lower latency.
   */
  void setReducePowerAtCostOfLatency(bool enabled)
  {
    _reducePowerAtCostOfLatency = enabled;
    if (enabled)
    {
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      esp_wifi_set_max_tx_power(44);
    }
    else
    {
      esp_wifi_set_ps(WIFI_PS_NONE);
      esp_wifi_set_max_tx_power(84);
    }
  }

  /**
   * @brief Returns the last requested power-saving state.
   * @return `true` when power saving is enabled.
   */
  bool getReducePowerAtCostOfLatency() const
  {
    return _reducePowerAtCostOfLatency;
  }

  /*
   * Peer list: the loop task and the ESP-NOW receive callback (WiFi task) both
   * read and change it. Every access to _peers/_peersCount happens inside a
   * short critical section that only copies memory; ESP-NOW calls and logging
   * run outside it, and senders work on a snapshot.
   */

  /**
   * @brief Registers an ESP-NOW peer for MIDI transmission.
   * @param macAddress Six-byte Wi-Fi MAC address of the peer.
   * @return `true` when the peer was added to ESP-NOW and the local peer list;
   * `false` when it already exists, the peer limit is reached or ESP-NOW rejects it.
   */
  bool addPeer(const uint8_t macAddress[6])
  {
    if (!macAddress)
    {
      return false;
    }
    if (getPeersCount() >= MAX_PEERS)
    {
      EspNowMidiLog::w("Maximum number of peers reached");
      return false;
    }

    EspNowMidiLog::mac("Adding peer: ", macAddress);

    if (addDriverPeer(macAddress) != ESP_OK)
    {
      EspNowMidiLog::e("Failed to add peer");
      return false;
    }

    // ESP-NOW accepted the MAC; another task may have filled the list meanwhile.
    int count;
    {
      EspNowMidiLock lock(_peersMux);
      count = _peersCount;
      if (count < MAX_PEERS)
      {
        memcpy(_peers[count].mac, macAddress, 6);
        _peers[count].packed_mac = PeerInfo::packMac(macAddress);
        _peers[count].lastSeenMs = 0;
        _peers[count].seen = false;
        _peers[count].missed = 0;
        _peers[count].unreachable = false;
        _peers[count].nextProbeMs = 0;
        count = ++_peersCount;
      }
      else
      {
        count = -1;
      }
    }
    if (count < 0)
    {
      esp_now_del_peer(macAddress);
      EspNowMidiLog::w("Maximum number of peers reached");
      return false;
    }
    EspNowMidiLog::i("Peer added successfully. Total peers: %d", count);
    return true;
  }

  /** @brief Removes every registered peer from ESP-NOW and the local list. */
  void clearPeers()
  {
    EspNowMidiLog::i("Clearing all peers from ESP-NOW...");

    uint8_t macs[MAX_PEERS][6];
    int count;
    {
      EspNowMidiLock lock(_peersMux);
      count = copyPeersLocked(macs, MAX_PEERS);
      memset(_peers, 0, sizeof(_peers));
      _peersCount = 0;
    }

    for (int i = 0; i < count; i++)
    {
      const esp_err_t result = esp_now_del_peer(macs[i]);
      if (result == ESP_OK)
      {
        EspNowMidiLog::mac("Removed peer: ", macs[i]);
      }
      else
      {
        EspNowMidiLog::e("Failed to remove peer, error: %d", result);
      }
    }

    EspNowMidiLog::i("All peers cleared");
  }

  /**
   * @brief Gets the number of registered peers.
   * @return Current peer count.
   */
  int getPeersCount() const
  {
    EspNowMidiLock lock(_peersMux);
    return _peersCount;
  }

  /**
   * @deprecated The pointer refers to the live list and can change under a
   * concurrent add or remove. Use getPeer(int, uint8_t[6]) or copyPeers().
   */
  [[deprecated("use getPeer(index, mac) or copyPeers()")]]
  const uint8_t *getPeer(int index) const
  {
    EspNowMidiLock lock(_peersMux);
    if (index < 0 || index >= _peersCount)
    {
      return nullptr;
    }
    return _peers[index].mac;
  }

  /**
   * @brief Copies the MAC address of a registered peer.
   * @param index Peer index in `[0, getPeersCount())`.
   * @param out Receives the 6-byte MAC.
   * @return `false` when @p index is out of range.
   */
  bool getPeer(int index, uint8_t out[6]) const
  {
    EspNowMidiLock lock(_peersMux);
    if (!out || index < 0 || index >= _peersCount)
    {
      return false;
    }
    memcpy(out, _peers[index].mac, 6);
    return true;
  }

  /**
   * @brief Copies all registered peer MACs (a consistent snapshot).
   * @param out Receives up to @p max MACs.
   * @param max Capacity of @p out.
   * @return Number of MACs copied.
   */
  int copyPeers(uint8_t out[][6], int max) const
  {
    EspNowMidiLock lock(_peersMux);
    return copyPeersLocked(out, max);
  }

  /**
   * @brief Removes a registered peer from ESP-NOW and the local list.
   * @param macAddress Six-byte Wi-Fi MAC address of the peer.
   * @return `true` when the peer was found and removed.
   */
  bool removePeer(const uint8_t macAddress[6])
  {
    if (!macAddress || !hasPeer(macAddress))
    {
      return false;
    }

    const esp_err_t result = esp_now_del_peer(macAddress);
    if (result != ESP_OK)
    {
      EspNowMidiLog::e("Failed to remove peer, error: %d", result);
      return false;
    }

    {
      EspNowMidiLock lock(_peersMux);
      const int index = indexOfLocked(PeerInfo::packMac(macAddress));
      if (index >= 0)
      {
        for (int i = index; i < _peersCount - 1; i++)
        {
          _peers[i] = _peers[i + 1];
        }
        _peersCount--;
        memset(&_peers[_peersCount], 0, sizeof(_peers[_peersCount]));
      }
    }
    EspNowMidiLog::mac("Removed peer: ", macAddress);
    return true;
  }

  /**
   * @brief Removes a registered peer by list index.
   * @param index Peer index in `[0, getPeersCount())`.
   * @return `true` when the peer was found and removed.
   */
  bool removePeer(int index)
  {
    uint8_t mac[6];
    return getPeer(index, mac) && removePeer(mac);
  }

  /** @brief Logs every registered peer MAC address. */
  void printPeers() const
  {
    uint8_t macs[MAX_PEERS][6];
    const int count = copyPeers(macs, MAX_PEERS);
    EspNowMidiLog::i("=== Registered ESP-NOW Peers ===");
    for (int i = 0; i < count; i++)
    {
      char macBuf[EspNowMidiLog::MAC_STR_LEN];
      EspNowMidiLog::formatMac(macBuf, sizeof(macBuf), macs[i]);
      EspNowMidiLog::i("Peer %d: %s", i, macBuf);
    }
    EspNowMidiLog::i("================================");
  }

  /**
   * @brief Sends raw data to every registered ESP-NOW peer.
   *
   * Peers marked unreachable are skipped, except for a probe now and then
   * (see isPeerReachable()).
   * @param data Bytes to transmit.
   * @param len Number of bytes in @p data.
   * @return `ESP_OK` when all sends are accepted (with the send queue: queued),
   * `ESP_FAIL` when no peers are registered or all are unreachable,
   * `ESP_ERR_ESPNOW_NO_MEM` when the send queue is full, or the last ESP-NOW
   * error encountered.
   */
  esp_err_t sendToAllPeers(const uint8_t *data, size_t len)
  {
    return sendToPeersExcept(data, len, [](const uint8_t *) { return false; });
  }

  /**
   * @brief Like sendToAllPeers(), but leaves out peers for which
   * @p exclude(mac) returns `true` (e.g. muted ones).
   */
  template <typename Exclude>
  esp_err_t sendToPeersExcept(const uint8_t *data, size_t len, Exclude exclude)
  {
    if (!ESP_NOW_MIDI_SEND_QUEUE)
    {
      service();
    }
    uint8_t macs[MAX_PEERS][6];
    int reachable = 0;
    int probes = 0;
    const int count = copySendTargets(macs, reachable, probes);
    if (count == 0)
    {
      countSend(ESP_FAIL);
      return ESP_FAIL;
    }

    // Reachable peers come first, so a probe's slow "not delivered" never
    // holds them up. Probe results only count when nothing else went out.
    esp_err_t result = ESP_OK;
    for (int i = 0; i < reachable + probes; i++)
    {
      if (exclude(macs[i]))
      {
        continue;
      }
      const esp_err_t err = deliver(macs[i], data, len, i >= reachable);
      if (err != ESP_OK && (i < reachable || reachable == 0))
      {
        result = err; // Return last error if any
      }
    }
    kickSender();
    if (reachable + probes < count)
    {
      EspNowMidiLock lock(_statsMux);
      _stats.skipped += count - reachable - probes;
    }
    if (reachable == 0 && probes == 0)
    {
      return ESP_FAIL; // every peer unreachable, no probe due
    }
    return result;
  }

  /**
   * @brief Sends raw data to a single ESP-NOW peer.
   * @param macAddress Six-byte Wi-Fi MAC address of the peer.
   * @param data Bytes to transmit.
   * @param len Number of bytes in @p data.
   * @return ESP-NOW send result (with the send queue: whether it was queued).
   */
  esp_err_t send(const uint8_t macAddress[6], const uint8_t *data, size_t len)
  {
    if (!macAddress || !data)
    {
      return ESP_ERR_INVALID_ARG;
    }
    if (!ESP_NOW_MIDI_SEND_QUEUE)
    {
      service();
    }
    else if (!hasPeer(macAddress))
    {
      return countSend(ESP_ERR_ESPNOW_NOT_FOUND); // as the driver would say
    }
    const esp_err_t err = deliver(macAddress, data, len, false);
    kickSender();
    return err;
  }

  /**
   * @brief Waits until every queued packet was sent and confirmed, e.g.
   * before deep sleep or a restart.
   * @param timeoutMs Longest wait.
   * @return `true` when nothing is left to send.
   */
  bool flush(uint32_t timeoutMs = 500)
  {
    const uint32_t start = static_cast<uint32_t>(millis());
    for (;;)
    {
      kickSender();
      if (pendingSends() == 0 && inFlightCount() == 0)
      {
        return true;
      }
      if (static_cast<uint32_t>(millis()) - start >= timeoutMs || !pauseForSender())
      {
        return pendingSends() == 0 && inFlightCount() == 0;
      }
    }
  }

  /** @brief Packets waiting in the send queue (0 without the queue). */
  size_t pendingSends() const
  {
    EspNowMidiLock lock(_queueMux);
    return _queue.size();
  }

  /**
   * @brief Sends a MIDI Note On message to all peers.
   * @param note MIDI note number.
   * @param velocity Note velocity.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendNoteOn(byte note, byte velocity, byte channel)
  {
    midi_message message;
    message.channel = channel;
    message.status = MIDI_NOTE_ON;
    message.firstByte = note;
    message.secondByte = velocity;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends a MIDI Note Off message to all peers.
   * @param note MIDI note number.
   * @param velocity Release velocity.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendNoteOff(byte note, byte velocity, byte channel)
  {
    midi_message message;
    message.channel = channel;
    message.status = MIDI_NOTE_OFF;
    message.firstByte = note;
    message.secondByte = velocity;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends a MIDI Control Change message to all peers.
   * @param control Controller number.
   * @param value Controller value.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendControlChange(byte control, byte value, byte channel)
  {
    midi_message message;
    message.channel = channel;
    message.status = MIDI_CONTROL_CHANGE;
    message.firstByte = control;
    message.secondByte = value;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends a MIDI Program Change message to all peers.
   * @param program Program number.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendProgramChange(byte program, byte channel)
  {
    midi_message message;
    message.channel = channel;
    message.status = MIDI_PROGRAM_CHANGE;
    message.firstByte = program;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends channel aftertouch to all peers.
   * @param pressure Channel pressure.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendAfterTouch(byte pressure, byte channel)
  {
    midi_message message;
    message.channel = channel;
    message.status = MIDI_AFTERTOUCH;
    message.firstByte = pressure;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends polyphonic aftertouch for one note to all peers.
   * @param note MIDI note number.
   * @param pressure Per-note pressure.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendAfterTouch(byte note, byte pressure, byte channel)
  {
    midi_message message;
    message.channel = channel;
    message.status = MIDI_POLY_AFTERTOUCH;
    message.firstByte = note;
    message.secondByte = pressure;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends polyphonic aftertouch to all peers.
   * @param note MIDI note number.
   * @param pressure Per-note pressure.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   * @see sendAfterTouch(byte, byte, byte)
   */
  inline esp_err_t sendAfterTouchPoly(byte note, byte pressure, byte channel)
  {
    return sendAfterTouch(note, pressure, channel);
  }

  /**
   * @brief Sends a raw 14-bit MIDI pitch-bend value to all peers.
   * @param value Wire-format pitch bend from `0` to `16383`; `8192` is center.
   * Values are masked to 14 bits.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   * @see sendPitchBend(int16_t, byte)
   */
  inline esp_err_t sendPitchBendRaw(int value, byte channel)
  {
    // Wire-format 14-bit value: 0..16383, center (no bend) = 8192.
    // Prefer sendPitchBend() unless you already have raw MIDI bytes.
    midi_message message;
    message.channel = channel;
    message.status = MIDI_PITCH_BEND;
    value = value & 0x3FFF;
    message.firstByte = value & 0x7F;
    message.secondByte = (value >> 7) & 0x7F;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends signed MIDI pitch bend to all peers.
   * @param value Pitch bend from `-8192` to `8191`; `0` is center. Values
   * outside this range are clamped.
   * @param channel MIDI channel.
   * @return ESP-NOW send result.
   *
   * This representation matches the FortySevenEffects MIDI library and the
   * value delivered to setHandlePitchBend().
   */
  inline esp_err_t sendPitchBend(int16_t value, byte channel)
  {
    // Signed pitch bend: -8192..8191, center (no bend) = 0.
    // Matches FortySevenEffects MIDI library send/receive callbacks.
    // clamp to signed 14-bit range
    if (value < -8192)
      value = -8192;
    if (value > 8191)
      value = 8191;

    // translate signed (-8192..8191) -> unsigned (0..16383)
    uint16_t raw = value + 8192;
    return sendPitchBendRaw(raw, channel);
  }

  /** @brief Sends the MIDI Start real-time message to all peers.
   * @return ESP-NOW send result. */
  inline esp_err_t sendStart()
  {
    midi_message message;
    message.channel = 0; // System messages don't use channel
    message.status = MIDI_START;
    message.firstByte = 0;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /** @brief Sends the MIDI Stop real-time message to all peers.
   * @return ESP-NOW send result. */
  inline esp_err_t sendStop()
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_STOP;
    message.firstByte = 0;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /** @brief Sends the MIDI Continue real-time message to all peers.
   * @return ESP-NOW send result. */
  inline esp_err_t sendContinue()
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_CONTINUE;
    message.firstByte = 0;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /** @brief Sends the MIDI Timing Clock real-time message to all peers.
   * @return ESP-NOW send result. */
  inline esp_err_t sendClock()
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_TIME_CLOCK;
    message.firstByte = 0;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends a MIDI Song Position Pointer message to all peers.
   * @param value Song position; only its lower 14 bits are sent.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendSongPosition(uint16_t value)
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_SONG_POS_POINTER;
    value = value & 0x3FFF;
    message.firstByte = value & 0x7F;
    message.secondByte = (value >> 7) & 0x7F;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends a MIDI Song Select message to all peers.
   * @param value Song number; only its lower 7 bits are sent.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendSongSelect(uint8_t value)
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_SONG_SELECT;
    value = value & 0x7F;
    message.firstByte = value;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /** @brief Sends a MIDI Tune Request message to all peers.
   * @return ESP-NOW send result. */
  inline esp_err_t sendTuneRequest()
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_TUNE_REQUEST;
    message.firstByte = 0;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends a MIDI Time Code Quarter Frame message to all peers.
   * @param value Encoded quarter-frame value; only its lower 7 bits are sent.
   * @return ESP-NOW send result.
   */
  inline esp_err_t sendTimeCode(uint8_t value)
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_TIME_CODE;
    value = value & 0x7F;
    message.firstByte = value;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /** @brief Sends a MIDI Active Sensing message to all peers.
   * @return ESP-NOW send result. */
  inline esp_err_t sendActiveSensing()
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_ACTIVE_SENSING;
    message.firstByte = 0;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }
  /** @brief Sends a MIDI System Reset message to all peers.
   * @return ESP-NOW send result. */
  inline esp_err_t sendSystemReset()
  {
    midi_message message;
    message.channel = 0;
    message.status = MIDI_SYSTEM_RESET;
    message.firstByte = 0;
    message.secondByte = 0;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  /**
   * @brief Sends a SysEx payload to all peers (fragmented if needed).
   * @param data Complete SysEx bytes, typically including `F0`…`F7`.
   * @param length Number of bytes in @p data (1..1024).
   * @return ESP-NOW send result of the last fragment, or `ESP_ERR_INVALID_ARG`
   * when length is out of range / encode fails; `ESP_FAIL` when any fragment fails.
   *
   * Large SysEx is split into versioned ESP-NOW frames (see esp_now_midi_sysex.h).
   * Fragment sends are sequential and may briefly delay voice messages.
   */
  inline esp_err_t sendSysex(const uint8_t *data, uint16_t length)
  {
    if (data == nullptr || length == 0 || length > esp_now_midi_sysex::MAX_MESSAGE)
      return ESP_ERR_INVALID_ARG;

    const uint8_t total = esp_now_midi_sysex::fragmentCount(length);
    uint8_t frame[esp_now_midi_sysex::MAX_FRAME];
    esp_err_t lastErr = ESP_OK;

    for (uint8_t seq = 0; seq < total; ++seq)
    {
      const uint16_t offset = esp_now_midi_sysex::fragmentPayloadOffset(seq);
      const uint16_t payloadLen = esp_now_midi_sysex::fragmentPayloadLength(length, seq);
      const size_t frameLen = esp_now_midi_sysex::encodeFrame(
          frame, sizeof(frame), seq, total, length, data + offset, payloadLen);
      if (frameLen == 0)
        return ESP_ERR_INVALID_ARG;

      lastErr = sendToAllPeers(frame, frameLen);
      if (lastErr != ESP_OK)
        return lastErr;
    }
    return lastErr;
  }

  /**
   * @brief Dispatches an incoming ESP-NOW MIDI packet to its registered handler.
   * @param mac Source MAC address.
   * @param incomingData Received ESP-NOW payload.
   * @param len Number of received bytes.
   *
   * This is called by the ESP-NOW receive callback. When automatic peer
   * discovery is enabled, unknown senders are added before dispatch. Prefer
   * the `setHandle*` methods for application-level MIDI handling.
   */
  void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
  {
    if (mac)
    {
      memcpy(_lastSenderMac, mac, 6);
      _lastSenderValid = true;
    }
    if (!incomingData || len <= 0)
    {
      return;
    }
    if (isMidiPacket(incomingData, len))
    {
      if (_autoPeerDiscovery && !hasPeer(mac))
      {
        addPeer(mac);
      }
      markSeen(mac);
    }
    // SysEx transport frames are always longer than short MIDI (1–3 bytes).
    if (len > static_cast<int>(sizeof(midi_message_packet)))
    {
      const uint8_t *complete = nullptr;
      uint16_t completeLen = 0;
      if (_sysexReassembler.feed(mac, incomingData, len, static_cast<uint32_t>(millis()),
                                complete, completeLen))
      {
        if (onSysExHandler)
          onSysExHandler(const_cast<uint8_t *>(complete), completeLen);
        if (onSysExFromHandler)
          onSysExFromHandler(mac, const_cast<uint8_t *>(complete), completeLen);
      }
      return;
    }

    // Convert variable-length packet to internal message format
    midi_message_packet packet;
    memset(&packet, 0, sizeof(packet)); // Zero out the packet first
    memcpy(&packet, incomingData, len); // Copy only received bytes
    midi_message message = packet.toMessage();

    if (onMessageHandler)
    {
      onMessageHandler(mac, message);
    }

    switch (message.status)
    {
    case MIDI_NOTE_ON:
      if (onNoteOnHandler)
        onNoteOnHandler(message.channel, message.firstByte, message.secondByte);
      break;
    case MIDI_NOTE_OFF:
      if (onNoteOffHandler)
        onNoteOffHandler(message.channel, message.firstByte, message.secondByte);
      break;
    case MIDI_CONTROL_CHANGE:
      if (onControlChangeHandler)
        onControlChangeHandler(message.channel, message.firstByte, message.secondByte);
      break;
    case MIDI_PROGRAM_CHANGE:
      if (onProgramChangeHandler)
        onProgramChangeHandler(message.channel, message.firstByte);
      break;
    case MIDI_AFTERTOUCH:
      if (onAfterTouchChannelHandler)
        onAfterTouchChannelHandler(message.channel, message.firstByte);
      break;
    case MIDI_POLY_AFTERTOUCH:
      if (onAfterTouchPolyHandler)
        onAfterTouchPolyHandler(message.channel, message.firstByte, message.secondByte);
      break;
    case MIDI_PITCH_BEND:
    {
      int pitchBendValue = (message.secondByte << 7) | message.firstByte;
      int16_t signedValue = pitchBendValue - 8192;
      if (onPitchBendHandler)
        onPitchBendHandler(message.channel, signedValue);
      break;
    }
    case MIDI_START:
      if (onStartHandler)
        onStartHandler();
      break;
    case MIDI_STOP:
      if (onStopHandler)
        onStopHandler();
      break;
    case MIDI_CONTINUE:
      if (onContinueHandler)
        onContinueHandler();
      break;
    case MIDI_TIME_CLOCK:
      if (onClockHandler)
        onClockHandler();
      break;
    case MIDI_SONG_POS_POINTER:
    {
      int songPosValue = (message.secondByte << 7) | message.firstByte;
      if (onSongPositionHandler)
        onSongPositionHandler(songPosValue);
      break;
    }
    case MIDI_SONG_SELECT:
      if (onSongSelectHandler)
        onSongSelectHandler(message.firstByte);
      break;
    case MIDI_TIME_CODE:
      if (onTimeCodeHandler)
        onTimeCodeHandler(message.firstByte);
      break;
    case MIDI_ACTIVE_SENSING:
      if (onActiveSensingHandler)
        onActiveSensingHandler();
      break;
    case MIDI_SYSTEM_RESET:
      if (onSystemResetHandler)
        onSystemResetHandler();
      break;
    case MIDI_UNKNOWN:
    case MIDI_SYSEX:
    case MIDI_TUNE_REQUEST:
    case SYSEX_END:
    default:
      break;
    }
  }

  /**
   * @brief Registers a Note On receive handler.
   * @param callback Called with channel, note, and velocity; pass `nullptr` to clear it.
   */
  void setHandleNoteOn(void (*callback)(byte channel, byte note, byte velocity))
  {
    onNoteOnHandler = callback;
  }

  /**
   * @brief Registers a Note Off receive handler.
   * @param callback Called with channel, note, and velocity; pass `nullptr` to clear it.
   */
  void setHandleNoteOff(void (*callback)(byte channel, byte note, byte velocity))
  {
    onNoteOffHandler = callback;
  }

  /**
   * @brief Registers a Control Change receive handler.
   * @param callback Called with channel, controller number, and value; pass `nullptr` to clear it.
   */
  void setHandleControlChange(void (*callback)(byte channel, byte control, byte value))
  {
    onControlChangeHandler = callback;
  }

  /**
   * @brief Registers a Program Change receive handler.
   * @param callback Called with channel and program number; pass `nullptr` to clear it.
   */
  void setHandleProgramChange(void (*callback)(byte channel, byte program))
  {
    onProgramChangeHandler = callback;
  }

  /**
   * @brief Registers a signed pitch-bend receive handler.
   * @param callback Called with channel and pitch bend from `-8192` to `8191`;
   * `0` is center. Pass `nullptr` to clear it.
   */
  void setHandlePitchBend(void (*callback)(byte channel, int value))
  {
    onPitchBendHandler = callback;
  }

  /**
   * @brief Registers a channel-aftertouch receive handler.
   * @param callback Called with channel and pressure; pass `nullptr` to clear it.
   */
  void setHandleAfterTouchChannel(void (*callback)(byte channel, byte pressure))
  {
    onAfterTouchChannelHandler = callback;
  }

  /**
   * @brief Registers a polyphonic-aftertouch receive handler.
   * @param callback Called with channel, note, and pressure; pass `nullptr` to clear it.
   */
  void setHandleAfterTouchPoly(void (*callback)(byte channel, byte note, byte pressure))
  {
    onAfterTouchPolyHandler = callback;
  }

  /** @brief Registers a MIDI Start receive handler.
   * @param callback Called when Start is received; pass `nullptr` to clear it. */
  void setHandleStart(void (*callback)())
  {
    onStartHandler = callback;
  }

  /** @brief Registers a MIDI Stop receive handler.
   * @param callback Called when Stop is received; pass `nullptr` to clear it. */
  void setHandleStop(void (*callback)())
  {
    onStopHandler = callback;
  }

  /** @brief Registers a MIDI Continue receive handler.
   * @param callback Called when Continue is received; pass `nullptr` to clear it. */
  void setHandleContinue(void (*callback)())
  {
    onContinueHandler = callback;
  }

  /** @brief Registers a MIDI Timing Clock receive handler.
   * @param callback Called when Timing Clock is received; pass `nullptr` to clear it. */
  void setHandleClock(void (*callback)())
  {
    onClockHandler = callback;
  }

  /**
   * @brief Registers a Song Position Pointer receive handler.
   * @param callback Called with the decoded 14-bit song position; pass
   * `nullptr` to clear it.
   */
  void setHandleSongPosition(void (*callback)(uint16_t value))
  {
    onSongPositionHandler = callback;
  }

  /**
   * @brief Registers a Song Select receive handler.
   * @param callback Called with the song number; pass `nullptr` to clear it.
   */
  void setHandleSongSelect(void (*callback)(byte value))
  {
    onSongSelectHandler = callback;
  }

  /**
   * @brief Registers a MIDI Time Code Quarter Frame receive handler.
   * @param callback Called with the encoded quarter-frame value; pass `nullptr`
   * to clear it.
   */
  void setHandleTimeCode(void (*callback)(byte value))
  {
    onTimeCodeHandler = callback;
  }

  /** @brief Registers an Active Sensing receive handler.
   * @param callback Called when Active Sensing is received; pass `nullptr` to clear it. */
  void setHandleActiveSensing(void (*callback)())
  {
    onActiveSensingHandler = callback;
  }

  /** @brief Registers a System Reset receive handler.
   * @param callback Called when System Reset is received; pass `nullptr` to clear it. */
  void setHandleSystemReset(void (*callback)())
  {
    onSystemResetHandler = callback;
  }

  /**
   * @brief Registers a SysEx receive handler.
   * @param callback Called with a complete reconstituted SysEx buffer (1..1024
   * bytes) after fragment reassembly; pass `nullptr` to clear it.
   *
   * The buffer is owned by the transport and is only valid for the duration of
   * the callback.
   */
  void setHandleSysEx(void (*callback)(uint8_t *data, uint16_t length))
  {
    onSysExHandler = callback;
  }

  /**
   * @brief Registers a SysEx receive handler that also gets the sender MAC.
   *
   * Same as setHandleSysEx(), and called after it when both are set. Runs in
   * the ESP-NOW receive callback (WiFi task).
   *
   * @param callback Handler, or `nullptr` to remove it.
   */
  void setHandleSysExFrom(void (*callback)(const uint8_t *mac, uint8_t *data, uint16_t length))
  {
    onSysExFromHandler = callback;
  }

  /**
   * @brief Whether sends still go to a peer.
   *
   * After ESP_NOW_MIDI_UNREACHABLE_AFTER unacknowledged sends in a row, sends
   * skip the peer (counted in getSendStats().skipped) except for a probe every
   * ESP_NOW_MIDI_PROBE_INTERVAL_MS. It is reachable again as soon as it
   * acknowledges a packet or sends one.
   * @return `false` when @p mac is unreachable or not a peer.
   */
  bool isPeerReachable(const uint8_t mac[6]) const
  {
    if (!mac)
    {
      return false;
    }
    EspNowMidiLock lock(_peersMux);
    const int index = indexOfLocked(PeerInfo::packMac(mac));
    return index >= 0 && !_peers[index].unreachable;
  }

  /**
   * @brief When a peer last sent a valid packet.
   * @param mac Peer MAC address.
   * @param ms Set to the millis() of the last packet.
   * @return `false` when @p mac is not a peer or nothing arrived from it yet.
   */
  bool getPeerLastSeen(const uint8_t mac[6], uint32_t &ms) const
  {
    if (!mac)
    {
      return false;
    }
    EspNowMidiLock lock(_peersMux);
    const int index = indexOfLocked(PeerInfo::packMac(mac));
    if (index < 0 || !_peers[index].seen)
    {
      return false;
    }
    ms = _peers[index].lastSeenMs;
    return true;
  }

  /**
   * @brief When ESP-NOW last accepted a packet from this device for sending.
   * @param ms Set to the millis() of the last accepted send.
   * @return `false` when nothing was sent yet.
   */
  bool getLastSendTime(uint32_t &ms) const
  {
    EspNowMidiLock lock(_statsMux);
    ms = _lastSendMs;
    return _hasSent;
  }

  /**
   * @brief Restarts ESP-NOW when the driver stopped confirming sends.
   *
   * Under sustained load the ESP-NOW driver can stop calling the send callback;
   * it then keeps refusing every send (`ESP_ERR_ESPNOW_NO_MEM`) until reboot.
   * When packets wait for ESP_NOW_MIDI_STALL_MS without any confirmation, this
   * restarts ESP-NOW and registers the peers again (see getSendStats().restarts).
   * If no send was confirmed since the previous restart either, it also stops
   * and starts Wi-Fi: on hardware the stuck state survives an ESP-NOW restart.
   * With the send queue (ESP_NOW_MIDI_SEND_QUEUE) the sender task does this
   * check, and this only wakes it. Without the queue it is called by every
   * send and by enomik::Client / enomik::Dongle loop(); call it from loop()
   * yourself when using esp_now_midi directly and sending rarely.
   */
  void service()
  {
#if ESP_NOW_MIDI_HAS_TASKS
    if (_senderTask)
    {
      wakeSender(); // the sender task restarts ESP-NOW itself
      return;
    }
#endif
    checkStall();
    if (ESP_NOW_MIDI_SEND_QUEUE)
    {
      while (sendQueued())
      {
      }
    }
  }

private:
  void checkStall()
  {
    const uint32_t now = static_cast<uint32_t>(millis());
    {
      EspNowMidiLock lock(_statsMux);
      const bool waiting = _inFlight > 0 || _refusedSinceConfirm;
      if (!waiting || now - _lastConfirmMs < ESP_NOW_MIDI_STALL_MS)
      {
        return;
      }
      if (_stats.restarts > 0 && now - _lastRestartMs < ESP_NOW_MIDI_RESTART_BACKOFF_MS)
      {
        return;
      }
    }
    restartEspNow(_stats.restarts > 0 && !_confirmedSinceRestart);
  }

public:

  /**
   * @brief Registers one handler for every received MIDI message.
   *
   * Receives the decoded message and the sender MAC, before the per-type
   * `setHandle*` handlers. Runs in the ESP-NOW receive callback (WiFi task):
   * keep it short, e.g. hand the message to the loop task.
   *
   * @param callback Handler, or `nullptr` to remove it.
   */
  void setHandleMessage(void (*callback)(const uint8_t *mac, const midi_message &message))
  {
    onMessageHandler = callback;
  }

  /**
   * @brief Checks whether a MAC address is registered as a peer.
   * @param mac Six-byte Wi-Fi MAC address to look up.
   * @return `true` when the peer is registered.
   */
  bool hasPeer(const uint8_t mac[6]) const
  {
    if (!mac)
    {
      return false;
    }
    EspNowMidiLock lock(_peersMux);
    return indexOfLocked(PeerInfo::packMac(mac)) >= 0;
  }

  /**
   * @brief Checks whether an ESP-NOW payload is a packet this library sends.
   *
   * Either a 1-3 byte MIDI message whose length matches its status byte, or a
   * SysEx transport frame (see esp_now_midi_sysex.h).
   */
  static bool isMidiPacket(const uint8_t *data, int len)
  {
    if (!data || len <= 0)
    {
      return false;
    }
    if (len > static_cast<int>(sizeof(midi_message_packet)))
    {
      esp_now_midi_sysex::FrameView frame;
      return esp_now_midi_sysex::parseFrame(data, len, frame);
    }
    if (data[0] < 0x80)
    {
      return false;
    }
    switch (data[0])
    {
    case MIDI_SYSEX:
    case SYSEX_END:
    case 0xF4:
    case 0xF5:
    case 0xF9:
    case 0xFD:
      return false; // not a complete message, or undefined
    default:
      break;
    }
    midi_message_packet packet{};
    memcpy(&packet, data, len);
    if (packet.getDataSize() != len)
    {
      return false;
    }
    for (int i = 1; i < len; i++)
    {
      if (data[i] & 0x80)
      {
        return false;
      }
    }
    return true;
  }

  /**
   * @brief MAC of the peer that sent the packet currently being dispatched.
   * @return Pointer to 6 bytes, or `nullptr` if no packet has been received yet.
   */
  const uint8_t *lastSenderMac() const
  {
    return _lastSenderValid ? _lastSenderMac : nullptr;
  }

  /**
   * @brief Send counters since boot, to tell why messages do not arrive:
   * refused sends (`failed`, `lastError`), or sent but not acknowledged by the
   * receiver (`notDelivered`, e.g. out of range or receiver off).
   */
  EspNowSendStats getSendStats() const
  {
    EspNowMidiLock lock(_statsMux);
    return _stats;
  }

private:
  EspNowSendStats _stats;         // Guarded by _statsMux
  mutable portMUX_TYPE _statsMux = portMUX_INITIALIZER_UNLOCKED;

  esp_err_t countSend(esp_err_t err)
  {
    EspNowMidiLock lock(_statsMux);
    if (err == ESP_OK)
    {
      ++_stats.sent;
      _lastSendMs = static_cast<uint32_t>(millis());
      _hasSent = true;
    }
    else
    {
      ++_stats.failed;
      _stats.lastError = err;
    }
    return err;
  }

  PeerInfo _peers[MAX_PEERS];     // Guarded by _peersMux
  int _peersCount = 0;            // Guarded by _peersMux
  mutable portMUX_TYPE _peersMux = portMUX_INITIALIZER_UNLOCKED;

  bool inFlightFull() const
  {
    EspNowMidiLock lock(_statsMux);
    return _inFlight >= ESP_NOW_MIDI_MAX_IN_FLIGHT;
  }

  int inFlightCount() const
  {
    EspNowMidiLock lock(_statsMux);
    return _inFlight;
  }

  using SendQueue = esp_now_midi_queue::SendQueue<ESP_NOW_MIDI_QUEUE_SHORT, ESP_NOW_MIDI_QUEUE_FRAMES,
                                                  esp_now_midi_sysex::MAX_FRAME>;
  static constexpr uint8_t kProbe = 1; // queue flag: send even though the peer is unreachable
  SendQueue _queue;                    // Guarded by _queueMux
  mutable portMUX_TYPE _queueMux = portMUX_INITIALIZER_UNLOCKED;
#if ESP_NOW_MIDI_HAS_TASKS
  TaskHandle_t _senderTask = nullptr;
  TaskHandle_t _wifiTask = nullptr; // runs the ESP-NOW callbacks
#endif

  // Queues a packet (or, without the queue, sends it right away).
  esp_err_t deliver(const uint8_t mac[6], const uint8_t *data, size_t len, bool probe)
  {
    if (!ESP_NOW_MIDI_SEND_QUEUE)
    {
      return sendOne(mac, data, len);
    }
    // A note off waits a little for a free slot, a SysEx frame longer; but
    // never in the task that reports send results, which would wait for itself.
    const bool frame = len > 3;
    const uint32_t waitMs =
        !canWaitForSender() ? 0
        : frame             ? ESP_NOW_MIDI_FRAME_WAIT_MS
        : esp_now_midi_queue::classify(data, len) == esp_now_midi_queue::Priority::High
            ? ESP_NOW_MIDI_SEND_WAIT_MS
            : 0;
    const uint32_t start = static_cast<uint32_t>(millis());
    for (;;)
    {
      SendQueue::Result result;
      size_t waiting;
      {
        EspNowMidiLock lock(_queueMux);
        result = _queue.push(mac, data, len, probe ? kProbe : 0);
        waiting = _queue.size();
      }
      if (result != SendQueue::Result::Full)
      {
        EspNowMidiLock lock(_statsMux);
        if (result == SendQueue::Result::Replaced)
        {
          ++_stats.replaced;
        }
        else if (result == SendQueue::Result::TooLong)
        {
          ++_stats.failed;
          _stats.lastError = ESP_ERR_INVALID_ARG;
          return ESP_ERR_INVALID_ARG;
        }
        if (waiting > _stats.queuePeak)
        {
          _stats.queuePeak = static_cast<uint32_t>(waiting);
        }
        return ESP_OK;
      }
      if (static_cast<uint32_t>(millis()) - start >= waitMs || !makeRoom())
      {
        EspNowMidiLock lock(_statsMux);
        ++_stats.dropped;
        ++_stats.failed;
        _stats.lastError = ESP_ERR_ESPNOW_NO_MEM;
        return ESP_ERR_ESPNOW_NO_MEM;
      }
    }
  }

  // Sends the oldest queued packet, if no other one waits for its confirmation.
  // A packet for a peer that was removed, or became unreachable since it was
  // queued (and is no probe), is left out. @return whether one was taken.
  bool sendQueued()
  {
    if (inFlightFull())
    {
      return false;
    }
    uint8_t mac[6];
    uint8_t data[esp_now_midi_sysex::MAX_FRAME];
    uint8_t flags = 0;
    size_t len;
    {
      EspNowMidiLock lock(_queueMux);
      len = _queue.pop(mac, data, flags);
    }
    if (len == 0)
    {
      return false;
    }
    bool known;
    bool unreachable = false;
    {
      EspNowMidiLock lock(_peersMux);
      const int index = indexOfLocked(PeerInfo::packMac(mac));
      known = index >= 0;
      if (known)
      {
        unreachable = _peers[index].unreachable;
      }
    }
    if (!known)
    {
      return true;
    }
    if (unreachable && !(flags & kProbe))
    {
      EspNowMidiLock lock(_statsMux);
      ++_stats.skipped;
      return true;
    }
    sendOne(mac, data, len, false);
    return true;
  }

  // Lets the sender task (or, without one, service()) send what is queued.
  void kickSender()
  {
    if (!ESP_NOW_MIDI_SEND_QUEUE)
    {
      return;
    }
#if ESP_NOW_MIDI_HAS_TASKS
    if (_senderTask)
    {
      wakeSender();
      return;
    }
#endif
    service();
  }

  void wakeSender()
  {
#if ESP_NOW_MIDI_HAS_TASKS
    if (_senderTask)
    {
      xTaskNotifyGive(_senderTask);
    }
#endif
  }

  void noteWifiTask()
  {
#if ESP_NOW_MIDI_HAS_TASKS
    _wifiTask = xTaskGetCurrentTaskHandle();
#endif
  }

  bool canWaitForSender() const
  {
#if ESP_NOW_MIDI_HAS_TASKS
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    return self != _wifiTask && self != _senderTask;
#else
    return true;
#endif
  }

  // Gives the sender a moment. @return false when waiting cannot help.
  bool pauseForSender()
  {
#if ESP_NOW_MIDI_HAS_TASKS
    if (_senderTask)
    {
      wakeSender();
      vTaskDelay(1);
      return true;
    }
#endif
    service();
    return false;
  }

  // Full queue: lets the sender take a packet. @return false when it cannot.
  bool makeRoom()
  {
#if ESP_NOW_MIDI_HAS_TASKS
    if (_senderTask)
    {
      return pauseForSender();
    }
#endif
    checkStall();
    return sendQueued();
  }

  void startSender()
  {
#if ESP_NOW_MIDI_HAS_TASKS
    if (ESP_NOW_MIDI_SEND_QUEUE && !_senderTask)
    {
      if (xTaskCreate(senderTask, "espnow_midi_tx", ESP_NOW_MIDI_SENDER_STACK, this,
                      ESP_NOW_MIDI_SENDER_PRIORITY, &_senderTask) != pdPASS)
      {
        _senderTask = nullptr;
        EspNowMidiLog::e("Sender task not started; sending from service()");
      }
    }
#endif
  }

#if ESP_NOW_MIDI_HAS_TASKS
  // Sends the queue, one packet at a time: woken by a new packet or a send
  // confirmation; the timeout keeps the stall check running.
  static void senderTask(void *arg)
  {
    esp_now_midi *self = static_cast<esp_now_midi *>(arg);
    for (;;)
    {
      self->checkStall();
      if (!self->sendQueued())
      {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
      }
    }
  }
#endif

  static void pauseForConfirmation()
  {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    vTaskDelay(1); // a confirmation usually takes well under 1 ms
#endif
  }

  // Sends one packet, keeping at most ESP_NOW_MIDI_MAX_IN_FLIGHT unconfirmed:
  // waits up to ESP_NOW_MIDI_SEND_WAIT_MS for a pending confirmation, then drops.
  // The count goes up before esp_now_send(): the confirmation can arrive first.
  esp_err_t sendOne(const uint8_t mac[6], const uint8_t *data, size_t len, bool wait = true)
  {
    for (int i = 0; wait && i < ESP_NOW_MIDI_SEND_WAIT_MS && inFlightFull(); i++)
    {
      pauseForConfirmation();
    }
    {
      EspNowMidiLock lock(_statsMux);
      if (_inFlight >= ESP_NOW_MIDI_MAX_IN_FLIGHT)
      {
        ++_stats.throttled;
        ++_stats.failed;
        _stats.lastError = ESP_ERR_ESPNOW_NO_MEM;
        return ESP_ERR_ESPNOW_NO_MEM;
      }
      if (_inFlight == 0 && !_refusedSinceConfirm)
      {
        _lastConfirmMs = static_cast<uint32_t>(millis()); // stall timer starts now
      }
      ++_inFlight;
      _lastSentPacked = PeerInfo::packMac(mac);
    }
    const esp_err_t err = esp_now_send(mac, data, len);
    if (err != ESP_OK)
    {
      EspNowMidiLock lock(_statsMux);
      if (_inFlight > 0)
      {
        --_inFlight;
      }
      if (err == ESP_ERR_ESPNOW_NO_MEM)
      {
        _refusedSinceConfirm = true;
      }
    }
    return countSend(err);
  }

  static esp_err_t addDriverPeer(const uint8_t mac[6])
  {
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = ESP_NOW_MIDI_CHANNEL;
    peerInfo.encrypt = false;
    return esp_now_add_peer(&peerInfo);
  }

  void restartEspNow(bool withWifi)
  {
    EspNowMidiLog::w(withWifi ? "ESP-NOW still stuck; restarting Wi-Fi and ESP-NOW"
                              : "ESP-NOW stopped confirming sends; restarting it");
    uint8_t macs[MAX_PEERS][6];
    const int count = copyPeers(macs, MAX_PEERS);

    esp_now_deinit();
    if (withWifi)
    {
      esp_wifi_stop();
      esp_wifi_start();
    }
    const esp_err_t err = esp_now_init();
    if (err != ESP_OK)
    {
      EspNowMidiLog::e("ESP-NOW restart failed: %d", err);
    }
    esp_wifi_set_channel(ESP_NOW_MIDI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    setReducePowerAtCostOfLatency(_reducePowerAtCostOfLatency);
    esp_now_register_send_cb(SendCallbackAdapter);
    esp_now_register_recv_cb(OnDataRecvStatic);
    for (int i = 0; i < count; i++)
    {
      addDriverPeer(macs[i]);
    }

    const uint32_t now = static_cast<uint32_t>(millis());
    EspNowMidiLock lock(_statsMux);
    _inFlight = 0;
    _refusedSinceConfirm = false;
    _lastConfirmMs = now;
    _lastRestartMs = now;
    _confirmedSinceRestart = false;
    ++_stats.restarts;
    if (withWifi)
    {
      ++_stats.wifiRestarts;
    }
  }

  void markSeen(const uint8_t mac[6])
  {
    if (!mac)
    {
      return;
    }
    const uint32_t now = static_cast<uint32_t>(millis());
    EspNowMidiLock lock(_peersMux);
    const int index = indexOfLocked(PeerInfo::packMac(mac));
    if (index >= 0)
    {
      _peers[index].lastSeenMs = now;
      _peers[index].seen = true;
      _peers[index].missed = 0;
      _peers[index].unreachable = false;
    }
  }

  // Send callback (WiFi task): counts unacknowledged sends per peer
  void noteDelivery(uint64_t packed, bool delivered, uint32_t now)
  {
    EspNowMidiLock lock(_peersMux);
    const int index = indexOfLocked(packed);
    if (index < 0)
    {
      return;
    }
    PeerInfo &peer = _peers[index];
    if (delivered)
    {
      peer.missed = 0;
      peer.unreachable = false;
      return;
    }
    if (peer.missed < 255)
    {
      ++peer.missed;
    }
    if (ESP_NOW_MIDI_UNREACHABLE_AFTER > 0 && !peer.unreachable &&
        peer.missed >= ESP_NOW_MIDI_UNREACHABLE_AFTER)
    {
      peer.unreachable = true;
      peer.nextProbeMs = now + ESP_NOW_MIDI_PROBE_INTERVAL_MS;
    }
  }

  // Peers to send to: reachable ones first, then unreachable ones due for a
  // probe (whose next probe moves on). @return all peers, sent to or not.
  int copySendTargets(uint8_t out[][6], int &reachable, int &probes)
  {
    const uint32_t now = static_cast<uint32_t>(millis());
    uint8_t due[MAX_PEERS][6];
    reachable = 0;
    probes = 0;
    EspNowMidiLock lock(_peersMux);
    for (int i = 0; i < _peersCount; i++)
    {
      PeerInfo &peer = _peers[i];
      if (!peer.unreachable)
      {
        memcpy(out[reachable++], peer.mac, 6);
      }
      else if (static_cast<int32_t>(now - peer.nextProbeMs) >= 0)
      {
        peer.nextProbeMs = now + ESP_NOW_MIDI_PROBE_INTERVAL_MS;
        memcpy(due[probes++], peer.mac, 6);
      }
    }
    for (int i = 0; i < probes; i++)
    {
      memcpy(out[reachable + i], due[i], 6);
    }
    return _peersCount;
  }

  int indexOfLocked(uint64_t packed) const
  {
    for (int i = 0; i < _peersCount; i++)
    {
      if (_peers[i].packed_mac == packed)
      {
        return i;
      }
    }
    return -1;
  }

  int copyPeersLocked(uint8_t out[][6], int max) const
  {
    const int count = max <= 0 ? 0 : (_peersCount < max ? _peersCount : max);
    for (int i = 0; i < count; i++)
    {
      memcpy(out[i], _peers[i].mac, 6);
    }
    return count;
  }
  inline static esp_now_midi *_instance = nullptr; // Active instance for the C callbacks
  DataSentCallback userDataSentCallback = nullptr;
  bool _autoPeerDiscovery = true;
  bool _reducePowerAtCostOfLatency = false;
  uint8_t _lastSenderMac[6] = {0};
  bool _lastSenderValid = false;

  // MIDI Handlers
  void (*onNoteOnHandler)(byte channel, byte note, byte velocity) = nullptr;
  void (*onNoteOffHandler)(byte channel, byte note, byte velocity) = nullptr;
  void (*onControlChangeHandler)(byte channel, byte control, byte value) = nullptr;
  void (*onProgramChangeHandler)(byte channel, byte program) = nullptr;
  void (*onPitchBendHandler)(byte channel, int value) = nullptr;
  void (*onAfterTouchChannelHandler)(byte channel, byte value) = nullptr;
  void (*onAfterTouchPolyHandler)(byte channel, byte note, byte value) = nullptr;
  void (*onStartHandler)() = nullptr;
  void (*onStopHandler)() = nullptr;
  void (*onContinueHandler)() = nullptr;
  void (*onClockHandler)() = nullptr;
  void (*onSongPositionHandler)(uint16_t value) = nullptr;
  void (*onSongSelectHandler)(byte value) = nullptr;
  void (*onTimeCodeHandler)(byte value) = nullptr;
  void (*onActiveSensingHandler)() = nullptr;
  void (*onSystemResetHandler)() = nullptr;
  void (*onSysExHandler)(uint8_t *data, uint16_t length) = nullptr;
  void (*onSysExFromHandler)(const uint8_t *mac, uint8_t *data, uint16_t length) = nullptr;
  uint32_t _lastSendMs = 0;
  bool _hasSent = false;
  int _inFlight = 0;                 // sent, not yet confirmed (guarded by _statsMux)
  uint64_t _lastSentPacked = 0;      // peer of the last send, when the callback has no address
  uint32_t _lastConfirmMs = 0;       // last send confirmation, or start of waiting
  uint32_t _lastRestartMs = 0;
  bool _refusedSinceConfirm = false; // driver said NO_MEM since the last confirmation
  bool _confirmedSinceRestart = false;
  esp_now_midi_sysex::Reassembler _sysexReassembler;
  void (*onMessageHandler)(const uint8_t *mac, const midi_message &message) = nullptr;
};
