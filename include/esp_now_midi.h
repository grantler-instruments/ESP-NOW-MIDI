/**
 * @file esp_now_midi.h
 * @brief ESP-NOW transport and MIDI message API.
 */
#pragma once
/** Maximum number of ESP-NOW peers tracked by an instance. */
#define MAX_PEERS 20
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
#include "./esp_now_midi_log.h"
#include "./esp_now_midi_wifi.h"

#ifndef ARDUINO
using byte = uint8_t;
#endif

/** @brief Stored representation of an ESP-NOW peer. */
struct PeerInfo
{
  uint8_t mac[6];       ///< Peer Wi-Fi MAC address.
  uint64_t packed_mac; ///< MAC address packed into the lower 48 bits.

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
    if (_instance && _instance->userDataSentCallback)
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
   * @param autoPeerDiscovery Add an unknown message sender as a peer when it
   * first sends data.
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

    _peersCount = 0;

    // Register callbacks
    esp_now_register_send_cb(SendCallbackAdapter);
    esp_now_register_recv_cb(OnDataRecvStatic);
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

  /**
   * @brief Registers an ESP-NOW peer for MIDI transmission.
   * @param macAddress Six-byte Wi-Fi MAC address of the peer.
   * @return `true` when the peer was added to ESP-NOW and the local peer list;
   * `false` when the peer limit is reached or ESP-NOW rejects it.
   */
  bool addPeer(const uint8_t macAddress[6])
  {
    if (_peersCount >= MAX_PEERS)
    {
      EspNowMidiLog::w("Maximum number of peers reached");
      return false;
    }

    EspNowMidiLog::mac("Adding peer: ", macAddress);

    // Create the peer info structure
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, macAddress, 6); // Always use exact size (6 bytes)
    peerInfo.channel = ESP_NOW_MIDI_CHANNEL;
    peerInfo.encrypt = false;

    // Add the peer to ESP-NOW
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      EspNowMidiLog::e("Failed to add peer");
      return false;
    }

    // Store the peer in our array AFTER successful ESP-NOW registration
    memcpy(_peers[_peersCount].mac, macAddress, 6);
    _peers[_peersCount].packed_mac = PeerInfo::packMac(macAddress);
    _peersCount++;
    EspNowMidiLog::i("Peer added successfully. Total peers: %d", _peersCount);
    return true;
  }

  /** @brief Removes every registered peer from ESP-NOW and the local list. */
  void clearPeers()
  {
    EspNowMidiLog::i("Clearing all peers from ESP-NOW...");

    // Remove all peers from ESP-NOW
    for (int i = 0; i < _peersCount; i++)
    {
      esp_err_t result = esp_now_del_peer(_peers[i].mac);
      if (result == ESP_OK)
      {
        EspNowMidiLog::mac("Removed peer: ", _peers[i].mac);
      }
      else
      {
        EspNowMidiLog::e("Failed to remove peer, error: %d", result);
      }
    }

    // Clear the internal peer list
    memset(_peers, 0, sizeof(_peers));
    _peersCount = 0;

    EspNowMidiLog::i("All peers cleared");
  }

  /**
   * @brief Gets the number of registered peers.
   * @return Current peer count.
   */
  int getPeersCount() const
  {
    return _peersCount;
  }

  /** @brief Logs every registered peer MAC address. */
  void printPeers() const
  {
    EspNowMidiLog::i("=== Registered ESP-NOW Peers ===");
    for (int i = 0; i < _peersCount; i++)
    {
      char macBuf[EspNowMidiLog::MAC_STR_LEN];
      EspNowMidiLog::formatMac(macBuf, sizeof(macBuf), _peers[i].mac);
      EspNowMidiLog::i("Peer %d: %s", i, macBuf);
    }
    EspNowMidiLog::i("================================");
  }

  /**
   * @brief Sends raw data to every registered ESP-NOW peer.
   * @param data Bytes to transmit.
   * @param len Number of bytes in @p data.
   * @return `ESP_OK` when all sends are accepted, `ESP_FAIL` when no peers are
   * registered, or the last ESP-NOW error encountered.
   */
  esp_err_t sendToAllPeers(const uint8_t *data, size_t len)
  {
    esp_err_t result = ESP_OK;

    if (_peersCount == 0)
    {
      return ESP_FAIL;
    }

    for (int i = 0; i < _peersCount; i++)
    {
      esp_err_t err = esp_now_send(_peers[i].mac, data, len);
      if (err != ESP_OK)
      {
        result = err; // Return last error if any
      }
    }
    return result;
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
    if (_autoPeerDiscovery && !hasPeer(mac))
    {
      addPeer(mac);
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
      }
      return;
    }

    // Convert variable-length packet to internal message format
    midi_message_packet packet;
    memset(&packet, 0, sizeof(packet)); // Zero out the packet first
    memcpy(&packet, incomingData, len); // Copy only received bytes
    midi_message message = packet.toMessage();

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
   * @brief Checks whether a MAC address is registered as a peer.
   * @param mac Six-byte Wi-Fi MAC address to look up.
   * @return `true` when the peer is registered.
   */
  bool hasPeer(const uint8_t mac[6]) const
  {
    uint64_t packed = PeerInfo::packMac(mac);
    for (int i = 0; i < _peersCount; i++)
    {
      if (_peers[i].packed_mac == packed)
        return true;
    }
    return false;
  }

private:
  PeerInfo _peers[MAX_PEERS];     // Array to store peer info with optimized MAC storage
  int _peersCount;                // Current number of peers
  static esp_now_midi *_instance; // Static pointer to hold the instance
  DataSentCallback userDataSentCallback = nullptr;
  bool _autoPeerDiscovery = true;
  bool _reducePowerAtCostOfLatency = false;

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
  esp_now_midi_sysex::Reassembler _sysexReassembler;
};

esp_now_midi *esp_now_midi::_instance = nullptr;