#include "AudioTools.h" //https://github.com/pschatzmann/arduino-audio-tools
#include "enomik_client.h"

// I2S pin assignments (adjust for your board)
#define I2S_BCLK  37
#define I2S_WCLK  38
#define I2S_DOUT  17

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = { 0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C };

enomik::Client _client;

const int SAMPLE_RATE = 44100;
const int CHANNELS    = 2;
const int BITS        = 16;

SineWaveGenerator<int16_t> sineWave;
GeneratedSoundStream<int16_t> soundStream(sineWave);
I2SStream i2s;
StreamCopy copier(i2s, soundStream);

void onNoteOn(byte channel, byte note, byte velocity) {
  float freq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
  sineWave.setFrequency(freq);
  sineWave.setAmplitude(velocity / 127.0f);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  sineWave.setAmplitude(0.0f);
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.sample_rate    = SAMPLE_RATE;
  cfg.channels       = CHANNELS;
  cfg.bits_per_sample = BITS;
  cfg.pin_bck        = I2S_BCLK;
  cfg.pin_ws         = I2S_WCLK;
  cfg.pin_data       = I2S_DOUT;
  i2s.begin(cfg);

  sineWave.begin(CHANNELS, SAMPLE_RATE, 0);  // start silent
  soundStream.begin();

  _client.begin();
  _client.addPeer(peerMacAddress);
  _client.espnowMIDI.setHandleNoteOn(onNoteOn);
  _client.espnowMIDI.setHandleNoteOff(onNoteOff);

  delay(1000);
  _client.sendControlChange(127, 127, 16);
}

void loop() {
  _client.loop();
  copier.copy();
}
