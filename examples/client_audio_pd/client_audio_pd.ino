#include <ESPdLib.h>  //https://github.com/algomusic/ESPdLib
#include <LittleFS.h>
#include "enomik_client.h"

// Abstractions / patch loading run on the loop task; give it headroom.
SET_LOOP_TASK_STACK_SIZE(64 * 1024);

// MAX98357A / PCM5102 I2S pin assignments (adjust for your board)
#define I2S_BCLK 37
#define I2S_WCLK 38
#define I2S_DOUT 17

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = { 0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C };

enomik::Client _client;

// The patch is not pre-loaded onto the LittleFS partition. It is received at
// runtime via SysEx, written to LittleFS, then opened. Until a patch arrives,
// no patch is open and the engine outputs silence.
const char* PATCH_PATH = "/streamed.pd";  // path on LittleFS (without /littlefs prefix)
void* currentPatch = nullptr;

// Write patch text received over SysEx to LittleFS and (re)open it in Pd.
bool loadPatchFromBuffer(const uint8_t* patchText, size_t length) {
  File f = LittleFS.open(PATCH_PATH, "w");
  if (!f) {
    Serial.println("PD: failed to open patch file for writing");
    return false;
  }
  f.write(patchText, length);
  f.close();

  if (currentPatch) {
    Pd.closePatch(currentPatch);
    currentPatch = nullptr;
  }

  // ESPdLib mounts LittleFS at "/littlefs"; openPatch takes the bare filename.
  currentPatch = Pd.openPatch(PATCH_PATH + 1);  // skip leading '/'
  if (!currentPatch) {
    Serial.println("PD: openPatch failed");
    return false;
  }
  Serial.println("PD: patch loaded");
  return true;
}

// --- MIDI -> Pd routing -----------------------------------------------------
// Patches receive values via [r ...] objects, e.g. [r note], [r gate], [r cc7].

void onNoteOn(byte channel, byte note, byte velocity) {
  Pd.sendFloat("note", note);
  Pd.sendFloat("velocity", velocity);
  Pd.sendFloat("gate", velocity > 0 ? 1.0f : 0.0f);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  Pd.sendFloat("gate", 0.0f);
}

void onControlChange(byte channel, byte control, byte value) {
  char receiver[8];
  snprintf(receiver, sizeof(receiver), "cc%d", control);
  Pd.sendFloat(receiver, value);
}

void onPitchBend(byte channel, int value) {
  Pd.sendFloat("bend", value);
}

// --- SysEx: receive the Pd patch --------------------------------------------
// TODO: define the chunked SysEx transfer protocol and reassemble the full
// patch here, then call loadPatchFromBuffer(). For now this just logs incoming
// SysEx so the wiring is in place; library/system SysEx is still handled by the
// client internally (pin config, peers, version, etc.).
void onSysEx(uint8_t* data, unsigned int length) {
  Serial.printf("PD: SysEx received (%u bytes)\n", length);
  // loadPatchFromBuffer(patchText, patchLength);
}

void setup() {
  Serial.begin(115200);

  ESPdLib::Config config;
  config.sampleRate = 44100;
  config.numOutputChannels = 2;
  config.numInputChannels = 0;
  config.bclkPin = I2S_BCLK;
  config.wsPin = I2S_WCLK;
  config.doutPin = I2S_DOUT;
  config.dinPin = -1;

  if (!Pd.begin(config)) {
    Serial.println("PD: Pd.begin() failed");
  }

  _client.begin();
  _client.addPeer(peerMacAddress);
  _client.espnowMIDI.setHandleNoteOn(onNoteOn);
  _client.espnowMIDI.setHandleNoteOff(onNoteOff);
  _client.espnowMIDI.setHandleControlChange(onControlChange);
  _client.espnowMIDI.setHandlePitchBend(onPitchBend);
  _client.setHandleSysEx(onSysEx);

  delay(1000);
  _client.sendControlChange(127, 127, 16);
}

void loop() {
  _client.loop();
}
