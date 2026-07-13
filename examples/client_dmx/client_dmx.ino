/*
 * DMX512 client: MIDI (ESP-NOW) -> DMX512 output.
 * Hardware: Grove DMX512 (SN75176 RS-485) or any UART RS-485 transceiver.
 *
 * This sketch uses the built-in DMXSender.h (no extra library). If you prefer
 * an external library, two options that work with Grove/SN75176:
 *
 *   • luksal/ESP32-DMX (https://github.com/luksal/ESP32-DMX)
 *     Install: Arduino Library Manager "ESP32-DMX" or
 *              arduino-cli lib install https://github.com/luksal/ESP32-DMX.git
 *     Pins are fixed: TX=17, RX=16, DE=4 → wire Grove DE to GPIO 4.
 *     API: DMX::Initialize(DMXDirection::output); DMX::Write(channel, value);
 *     No update() in loop; it sends in a background task.
 *
 *   • pierrejay/esp32-EZDMX (https://github.com/pierrejay/esp32-EZDMX)
 *     Install: clone repo and add lib/EZDMX to your Arduino libraries folder,
 *              or add as dependency in PlatformIO.
 *     Configurable DE pin (e.g. 21 for Grove). API: dmx.begin(); dmx.start();
 *     dmx.set(channel, value);  (MIT license)
 *
 * Wiring (Grove DMX512 / SN75176) with this sketch’s DMXSender:
 *   - dmxTxPin (default GPIO 21) -> module RX/DI
 *     ESP32-S2 can map UART TX to any free GPIO; change dmxTxPin below if needed.
 *   - dePin (default GPIO 4) -> DE/RE (driver enable; high = transmit)
 *     Must be a different GPIO than dmxTxPin.
 *   - 3.3V, GND; DMX out from module A/B to fixture.
 */
#include "enomik_client.h"
#include "DMXSender.h"

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
enomik::Client _client;

DMXSender dmx;
HardwareSerial dmxSerial(1);  // UART1 (ESP32-S2/S3/C3 have no UART2; original ESP32 also works)
const uint8_t dmxTxPin = 21;  // UART TX -> RS-485 DI (any free GPIO on ESP32-S2)
const uint8_t dePin = 4;      // DE/RE for SN75176 (must differ from dmxTxPin)
const uint16_t numChannels = 512;

struct ChannelState {
  uint8_t msb = 0;
  uint8_t lsb = 0;
  bool msbReceived = false;
  bool lsbReceived = false;
  unsigned long lastUpdateTime = 0;
};

ChannelState channelStates[512];
const unsigned long PAIR_TIMEOUT = 20;  // ms - if both halves arrive within this window, combine them

// MIDI note-on start channel (1 = use MIDI ch 1–5 for DMX 1–512 via notes)
const int noteOnStartChannel = 1;

void onNoteOn(byte channel, byte note, byte velocity) {
  // Use MIDI channels noteOnStartChannel..(noteOnStartChannel+4) for DMX 1–512
  // Ch 1: DMX 1–127, Ch 2: 128–254, Ch 3: 255–381, Ch 4: 382–508, Ch 5: 509–512
  if (channel < noteOnStartChannel || channel > (noteOnStartChannel + 4)) {
    return;
  }
  if (note < 1 || note > 127) {
    return;
  }

  int dmxChannel = (channel - noteOnStartChannel) * 127 + note;
  if (dmxChannel >= 1 && dmxChannel <= 512) {
    dmx.writeByte(velocity * 2, dmxChannel);  // writeByte(data, channel); 0-127 -> 0-254
  }
}

void onNoteOff(byte channel, byte note, byte velocity) {
  if (channel < noteOnStartChannel || channel > (noteOnStartChannel + 4)) {
    return;
  }
  if (note < 1 || note > 127) {
    return;
  }

  int dmxChannel = (channel - noteOnStartChannel) * 127 + note;
  if (dmxChannel >= 1 && dmxChannel <= 512) {
    dmx.writeByte(0, dmxChannel);
  }
}
void onControlChange(byte channel, byte control, byte value) {
  // Determine if MSB (CC 0-31) or LSB (CC 32-63)
  bool isMSB = (control < 32);
  int baseControl = isMSB ? control : (control - 32);
  int dmxChannel = (channel - 1) * 32 + baseControl;

  if (dmxChannel >= 0 && dmxChannel < numChannels) {
    unsigned long now = millis();

    // Check if we should reset state due to timeout
    if (now - channelStates[dmxChannel].lastUpdateTime > PAIR_TIMEOUT) {
      channelStates[dmxChannel].msbReceived = false;
      channelStates[dmxChannel].lsbReceived = false;
    }

    if (isMSB) {
      channelStates[dmxChannel].msb = value;
      channelStates[dmxChannel].msbReceived = true;
      channelStates[dmxChannel].lastUpdateTime = now;
    } else {
      channelStates[dmxChannel].lsb = value;
      channelStates[dmxChannel].lsbReceived = true;
      channelStates[dmxChannel].lastUpdateTime = now;
    }

    // If we have both MSB and LSB, update DMX
    if (channelStates[dmxChannel].msbReceived && channelStates[dmxChannel].lsbReceived) {
      // Combine into 14-bit value
      uint16_t fullValue = (channelStates[dmxChannel].msb << 7) | channelStates[dmxChannel].lsb;

      // Scale to 8-bit DMX (0-16383 → 0-255)
      uint8_t dmxValue = fullValue >> 6;

      // DMX channels 1-indexed
      dmx.writeByte(dmxValue, dmxChannel + 1);

      // Clear flags for next pair
      channelStates[dmxChannel].msbReceived = false;
      channelStates[dmxChannel].lsbReceived = false;
    }
  }
}

void onProgramChange(byte channel, byte program) {
}

void onPitchBend(byte channel, int value) {}
void onAfterTouch(byte channel, byte value) {}
void onPolyAfterTouch(byte channel, byte note, byte value) {}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);


  _client.begin();
  _client.addPeer(peerMacAddress);

  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleNoteOff(onNoteOff);
  _client.setHandleControlChange(onControlChange);
  _client.setHandleProgramChange(onProgramChange);
  _client.setHandlePitchBend(onPitchBend);
  _client.setHandleAfterTouchChannel(onAfterTouch);
  _client.setHandleAfterTouchPoly(onPolyAfterTouch);

  // 250k baud, 8N2 (DMX512). RX unused (-1); TX on dmxTxPin (any GPIO works on ESP32-S2).
  dmxSerial.begin(250000, SERIAL_8N2, -1, dmxTxPin);
  dmx.begin(dmxSerial, dePin, numChannels);

  // register as a client by sending any message
  // this is needed in this case, as the client will stay unkown to the dongle until the first message is sent.
  _client.sendControlChange(127, 127, 16);
}

void loop() {
  _client.loop();
  dmx.update();
}
