#include "M16.h"//https://github.com/algomusic/M16
#include "Osc.h"
#include "Env.h"
#include "enomik_client.h"

// MAX98357A I2S pin assignments (adjust for your board)
#define I2S_BCLK  37
#define I2S_WCLK  38
#define I2S_DOUT  17

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = { 0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C };

enomik::Client _client;

Osc osc;
Env env;

void onNoteOn(byte channel, byte note, byte velocity) {
  osc.setPitch(note);
  env.setMaxLevel(velocity / 127.0f);
  env.start();
}

void onNoteOff(byte channel, byte note, byte velocity) {
  env.startRelease();
}

// M16 audio callback — runs automatically in a background FreeRTOS task
void audioUpdate() {
  // osc.next() → [-32767, 32767], env.next() → [0, 65534]; >> 16 scales back to 16-bit
  int16_t out = (int16_t)(((int32_t)osc.next() * env.next()) >> 16);
  i2s_write_samples(out, out);  // mono: same value on both channels
}

void setup() {
  Serial.begin(115200);

  // Pin audio to core 0 only — leaves core 1 free for ESP-NOW / Wi-Fi
  setIsDualCore(false);

  // Configure I2S pins before audioStart(); -1 = no audio input
  seti2sPins(I2S_BCLK, I2S_WCLK, I2S_DOUT, -1);

  osc.sinGen();     // build internal sine wavetable
  osc.setPitch(69); // A4 = 440 Hz

  env.setAttack(10);    // ms
  env.setDecay(100);    // ms
  env.setSustain(0.7f); // 0.0–1.0
  env.setRelease(300);  // ms

  audioStart();

  _client.begin();
  _client.addPeer(peerMacAddress);
  _client.espnowMIDI.setHandleNoteOn(onNoteOn);
  _client.espnowMIDI.setHandleNoteOff(onNoteOff);

  delay(1000);
  _client.sendControlChange(127, 127, 16);
}

void loop() {
  _client.loop();
}
