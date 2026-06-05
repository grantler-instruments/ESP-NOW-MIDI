#pragma once
#define MAX_PEERS 20
#ifndef ESP_NOW_MIDI_CHANNEL
#define ESP_NOW_MIDI_CHANNEL 6
#endif
#include "./version.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "./midiHelpers.h"
#define ESP_NOW_DEBUGGING 0

// Optimized peer storage with packed MAC address for fast comparison
struct PeerInfo
{
  uint8_t mac[6];
  uint64_t packed_mac; // Stored as 48-bit value in 64-bit integer

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

class esp_now_midi
{
public:
  typedef void (*DataSentCallback)(const wifi_tx_info_t *info, esp_now_send_status_t status);

  static void DefaultOnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
  {
#if ESP_NOW_DEBUGGING == 1
    Serial.print("\r\nLast Packet Send Status:\t");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
#endif
  }

  static void SendCallbackAdapter(const wifi_tx_info_t *info, esp_now_send_status_t status)
  {
    if (_instance && _instance->userDataSentCallback)
    {
      _instance->userDataSentCallback(info, status);
    }
  }

  static void OnDataRecvStatic(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len)
  {
    if (_instance)
    {
      _instance->OnDataRecv(recv_info->src_addr, incomingData, len);
    }
  }
  void begin(bool reducePowerAtCostOfLatency = false, bool autoPeerDiscovery = true, DataSentCallback callback = DefaultOnDataSent)
  {
    _instance = this;
    _autoPeerDiscovery = autoPeerDiscovery;
    userDataSentCallback = callback; // This needs to be INSIDE the function

    // Initialize WiFi if needed
    if (WiFi.getMode() != WIFI_MODE_STA)
    {
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      delay(100);
    }

    // Try to initialize ESP-NOW (gracefully handle if already initialized)
    esp_err_t init_result = esp_now_init();

    if (init_result == ESP_ERR_ESPNOW_EXIST)
    {
      Serial.println("[ESP-NOW] Already initialized");
    }
    else if (init_result != ESP_OK)
    {
      Serial.printf("[ESP-NOW] Init failed with error: %d\n", init_result);
      return;
    }

    // Set channel and power
    esp_wifi_set_channel(ESP_NOW_MIDI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (reducePowerAtCostOfLatency)
    {
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      esp_wifi_set_max_tx_power(44);
    }
    else
    {
      esp_wifi_set_ps(WIFI_PS_NONE);
      esp_wifi_set_max_tx_power(84);
    }

    _peersCount = 0;

    // Register callbacks
    esp_now_register_send_cb(SendCallbackAdapter);
    esp_now_register_recv_cb(OnDataRecvStatic);
  }

  // Add a new peer
  bool addPeer(const uint8_t macAddress[6])
  {
    if (_peersCount >= MAX_PEERS)
    {
      Serial.println("Maximum number of peers reached");
      return false;
    }

    // Debug print
    Serial.print("Adding peer: ");
    for (int i = 0; i < 6; i++)
    {
      Serial.print(macAddress[i], HEX);
      if (i < 5)
        Serial.print(":");
    }
    Serial.println();

    // Create the peer info structure
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, macAddress, 6); // Always use exact size (6 bytes)
    peerInfo.channel = ESP_NOW_MIDI_CHANNEL;
    peerInfo.encrypt = false;

    // Add the peer to ESP-NOW
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      Serial.println("Failed to add peer");
      return false;
    }

    // Store the peer in our array AFTER successful ESP-NOW registration
    memcpy(_peers[_peersCount].mac, macAddress, 6);
    _peers[_peersCount].packed_mac = PeerInfo::packMac(macAddress);
    _peersCount++;
    Serial.print("Peer added successfully. Total peers: ");
    Serial.println(_peersCount);
    return true;
  }

  void clearPeers()
  {
    Serial.println("Clearing all peers from ESP-NOW...");

    // Remove all peers from ESP-NOW
    for (int i = 0; i < _peersCount; i++)
    {
      esp_err_t result = esp_now_del_peer(_peers[i].mac);
      if (result == ESP_OK)
      {
        Serial.print("Removed peer: ");
        for (int j = 0; j < 6; j++)
        {
          Serial.print(_peers[i].mac[j], HEX);
          if (j < 5)
            Serial.print(":");
        }
        Serial.println();
      }
      else
      {
        Serial.printf("Failed to remove peer, error: %d\n", result);
      }
    }

    // Clear the internal peer list
    memset(_peers, 0, sizeof(_peers));
    _peersCount = 0;

    Serial.println("All peers cleared");
  }

  int getPeersCount() const
  {
    return _peersCount;
  }

  void printPeers() const
  {
    Serial.println("=== Registered ESP-NOW Peers ===");
    for (int i = 0; i < _peersCount; i++)
    {
      Serial.print("Peer ");
      Serial.print(i);
      Serial.print(": ");
      for (int j = 0; j < 6; j++)
      {
        Serial.print(_peers[i].mac[j], HEX);
        if (j < 5)
          Serial.print(":");
      }
      Serial.println();
    }
    Serial.println("================================");
  }

  // Send to all peers
  esp_err_t sendToAllPeers(const uint8_t *data, size_t len)
  {
    esp_err_t result = ESP_OK;

    if (_peersCount == 0)
    {
      // Serial.println("No peers registered!");
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

  inline esp_err_t sendAfterTouchPoly(byte note, byte pressure, byte channel)
  {
    return sendAfterTouch(note, pressure, channel);
  }

  inline esp_err_t sendPitchBendRaw(int value, byte channel)
  {
    midi_message message;
    message.channel = channel;
    message.status = MIDI_PITCH_BEND;
    value = value & 0x3FFF;
    message.firstByte = value & 0x7F;
    message.secondByte = (value >> 7) & 0x7F;

    midi_message_packet packet = midi_message_packet::fromMessage(message);
    return sendToAllPeers((uint8_t *)&packet, packet.getDataSize());
  }

  inline esp_err_t sendPitchBend(int16_t value, byte channel)
  {
    // clamp to signed 14-bit range
    if (value < -8192)
      value = -8192;
    if (value > 8191)
      value = 8191;

    // translate signed (-8192..8191) -> unsigned (0..16383)
    uint16_t raw = value + 8192;
    return sendPitchBendRaw(raw, channel);
  }

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

  inline esp_err_t sendSysex(uint8_t data[128], uint8_t length)
  {
    midi_sysex_message sysexMessage;
    sysexMessage.length = length;
    memcpy(sysexMessage.data, data, length);
    return sendToAllPeers((uint8_t *)&sysexMessage, sizeof(sysexMessage));
  }

  void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
  {
    if (_autoPeerDiscovery && !hasPeer(mac))
    {
      addPeer(mac);
    }
    // Handle SysEx separately (larger than 3 bytes)
    if (len > sizeof(midi_message_packet))
    {
      midi_sysex_message sysexMessage;
      memcpy(&sysexMessage, incomingData, sizeof(midi_sysex_message));
      // TODO: Handle SysEx message if needed
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
    }
  }

  void setHandleNoteOn(void (*callback)(byte channel, byte note, byte velocity))
  {
    onNoteOnHandler = callback;
  }

  void setHandleNoteOff(void (*callback)(byte channel, byte note, byte velocity))
  {
    onNoteOffHandler = callback;
  }

  void setHandleControlChange(void (*callback)(byte channel, byte control, byte value))
  {
    onControlChangeHandler = callback;
  }

  void setHandleProgramChange(void (*callback)(byte channel, byte program))
  {
    onProgramChangeHandler = callback;
  }

  void setHandlePitchBend(void (*callback)(byte channel, int value))
  {
    onPitchBendHandler = callback;
  }

  void setHandleAfterTouchChannel(void (*callback)(byte channel, byte pressure))
  {
    onAfterTouchChannelHandler = callback;
  }

  void setHandleAfterTouchPoly(void (*callback)(byte channel, byte note, byte pressure))
  {
    onAfterTouchPolyHandler = callback;
  }

  void setHandleStart(void (*callback)())
  {
    onStartHandler = callback;
  }

  void setHandleStop(void (*callback)())
  {
    onStopHandler = callback;
  }

  void setHandleContinue(void (*callback)())
  {
    onContinueHandler = callback;
  }

  void setHandleClock(void (*callback)())
  {
    onClockHandler = callback;
  }

  void setHandleSongPosition(void (*callback)(uint16_t value))
  {
    onSongPositionHandler = callback;
  }

  void setHandleSongSelect(void (*callback)(byte value))
  {
    onSongSelectHandler = callback;
  }

  void setHandleTimeCode(void (*callback)(byte value))
  {
    onTimeCodeHandler = callback;
  }

  void setHandleActiveSensing(void (*callback)())
  {
    onActiveSensingHandler = callback;
  }

  void setHandleSystemReset(void (*callback)())
  {
    onSystemResetHandler = callback;
  }

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
};

esp_now_midi *esp_now_midi::_instance = nullptr;