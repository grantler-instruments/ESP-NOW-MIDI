#include "enomik_client.h"


// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
enomik::Client _client;


void onStart() {
  bool success = _client.sendNoteOn(60, 127, 1);
  Serial.println("start");
}
void onStop() {
  bool success = _client.sendNoteOff(60, 0, 1);
  Serial.println("stop");
}
void onContinue() {
  bool success = _client.sendNoteOn(60, 127, 1);
  Serial.println("continue");
}

void onClock() {
  Serial.println("Clock");
}

void onSongPosition(int value) {
  bool success = _client.sendNoteOn(127, 127, 1);
  Serial.print("song position: ");
  Serial.print(value);
}

void onSongSelect(int value) {
  bool success = _client.sendNoteOn(126, 127, 1);
  Serial.print("song: ");
  Serial.print(value);
}

void onNoteOn(byte channel, byte note, byte velocity) {
  Serial.printf("Note On - Channel: %d, Note: %d, Velocity: %d\n", channel, note, velocity);
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Small delay to ensure Serial is ready
  Serial.println("Serial started");


  _client.setName("client_clocked"); // optional
  _client.begin();
  _client.addPeer(peerMacAddress);

  _client.setHandleStart(onStart);
  _client.setHandleStop(onStop);
  _client.setHandleContinue(onContinue);
  _client.setHandleClock(onClock);
  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleSongPosition(onSongPosition);
  _client.setHandleSongSelect(onSongSelect);
}

void loop() {
  _client.loop();
}