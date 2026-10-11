#include "enomik_client.h"


// on the dongle: run the print_mac firmware and paste it here
// uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
// uint8_t peerMacAddress[6] = { 0x24, 0x58, 0x7C, 0xEC, 0x6C, 0x24 };
// or set via the web interface

enomik::Client _client;

void onNoteOn(byte channel, byte note, byte velocity) {
  _client.sendNoteOn(note, velocity, channel);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  _client.sendNoteOff(note, velocity, channel);
}

void onControlChange(byte channel, byte control, byte value) {
  _client.sendControlChange(control, value, channel);
}

void onProgramChange(byte channel, byte program) {
  _client.sendProgramChange(program, channel);
}

void onPitchBend(byte channel, int value) {
  _client.sendPitchBend(value, channel);
}
void onAfterTouch(byte channel, byte value) {
  _client.sendAfterTouch(value, channel);
}
void onPolyAfterTouch(byte channel, byte note, byte value) {
  _client.sendPolyAfterTouch(note, value, channel);
}
void onStart() {
  _client.sendStart();
}
void onStop() {
  _client.sendStop();
}
void onContinue() {
  _client.sendContinue();
}
void onClock() {
  _client.sendClock();
}

void setup() {
  Serial.begin(115200);
  _client.setName("client_echo"); // optional
  _client.begin();
  // _client.addPeer(peerMacAddress);

  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleNoteOff(onNoteOff);
  _client.setHandleControlChange(onControlChange);
  _client.setHandleProgramChange(onProgramChange);
  _client.setHandlePitchBend(onPitchBend);
  _client.setHandleAfterTouchChannel(onAfterTouch);
  _client.setHandleAfterTouchPoly(onPolyAfterTouch);
  _client.setHandleStart(onStart);
  _client.setHandleStop(onStop);
  _client.setHandleContinue(onContinue);
  _client.setHandleClock(onClock);
}


void loop() {
  _client.loop();
}
