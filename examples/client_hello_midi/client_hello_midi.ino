#include "enomik_client.h"

// on the dongle: run the print_mac firmware and paste it here
// uint8_t peerMacAddress[6] = { 0x48, 0x27, 0xE2, 0x47, 0x3D, 0x74 };
uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
enomik::Client _client;
byte channel = 1;


void onNoteOn(byte channel, byte note, byte velocity) {
  Serial.printf("Note On - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  Serial.printf("Note Off - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
}

void onControlChange(byte channel, byte control, byte value) {
  Serial.printf("Control Change - Channel: %d, Control: %d, Value: %d\n", channel, control, value);
}

void onProgramChange(byte channel, byte program) {
  Serial.printf("Program Change - Channel: %d, Program: %d\n", channel, program);
}

void onPitchBend(byte channel, int value) {
  Serial.printf("Pitch Bend - Channel: %d, Value: %d\n", channel, value);
}
void onAfterTouch(byte channel, byte value) {
  Serial.printf("After Touch - Channel: %d, Value: %d\n", channel, value);
}
void onPolyAfterTouch(byte channel, byte note, byte value) {
  Serial.printf("Poly After Touch - Channel: %d, note: %d, Value: %d\n", channel, note, value);
}
void onStart() {
  Serial.printf("Start");
}
void onStop() {
  Serial.printf("Stop");
}
void onContinue() {
  Serial.printf("Continue");
}
void onClock() {
  Serial.printf("Clock");
}

void setup() {
  Serial.begin(115200);
  _client.begin();
  _client.addPeer(peerMacAddress);
  // all of these midi handlers are optional, depends on the usecase, very often you just wanna send data and not receive
  // e.g. this can be used for calibration, or maybe you wanna connect an amp via i2s and render some sound
  // _client.setHandleNoteOn(onNoteOn);
  // _client.setHandleNoteOff(onNoteOff);
  // _client.setHandleControlChange(onControlChange);
  // _client.setHandleProgramChange(onProgramChange);
  // _client.setHandlePitchBend(onPitchBend);
  // _client.setHandleAfterTouchChannel(onAfterTouch);
  // _client.setHandleAfterTouchPoly(onPolyAfterTouch);
  // _client.setHandleStart(onStart);
  // _client.setHandleStop(onStop);
  // _client.setHandleContinue(onContinue);
  // _client.setHandleClock(onClock);
}

void loop() {
  _client.loop();
  bool success = _client.sendNoteOn(60, 127, 1);

  if (success != ESP_OK) {
    Serial.println("Error sending the data");
  }
  delay(100);
  success = _client.sendNoteOff(60, 0, channel);
  delay(100);
  success = _client.sendControlChange(1, 127, channel);
  delay(100);
  success = _client.sendControlChange(1, 0, channel);
  delay(100);
  success = _client.sendPitchBend(-8192, channel);
  delay(100);
  success = _client.sendPitchBend(0, channel);
  delay(100);
  success = _client.sendPitchBend(8191, channel);

  delay(2000);
}