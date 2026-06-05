#include <esp_now.h>
#include <WiFi.h>
#include "esp_now_midi.h"


// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
esp_now_midi ESP_NOW_MIDI;

void customOnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failure");
}

void onNoteOn(byte channel, byte note, byte velocity) {
  ESP_NOW_MIDI.sendNoteOn(note, velocity, channel);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  ESP_NOW_MIDI.sendNoteOff(note, velocity, channel);
}

void onControlChange(byte channel, byte control, byte value) {
  ESP_NOW_MIDI.sendControlChange(control, value, channel);
}

void onProgramChange(byte channel, byte program) {
  ESP_NOW_MIDI.sendProgramChange(program, channel);
}

void onPitchBend(byte channel, int value) {
  ESP_NOW_MIDI.sendPitchBend(value, channel);
}
void onAfterTouch(byte channel, byte value) {
  ESP_NOW_MIDI.sendAfterTouch(value, channel);
}
void onPolyAfterTouch(byte channel, byte note, byte value) {
  ESP_NOW_MIDI.sendAfterTouch(note, value, channel);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  ESP_NOW_MIDI.begin(false, true, customOnDataSent);
  ESP_NOW_MIDI.addPeer(peerMacAddress);

  ESP_NOW_MIDI.setHandleNoteOn(onNoteOn);
  ESP_NOW_MIDI.setHandleNoteOff(onNoteOff);
  ESP_NOW_MIDI.setHandleControlChange(onControlChange);
  ESP_NOW_MIDI.setHandleProgramChange(onProgramChange);
  ESP_NOW_MIDI.setHandlePitchBend(onPitchBend);
  ESP_NOW_MIDI.setHandleAfterTouchChannel(onAfterTouch);
  ESP_NOW_MIDI.setHandleAfterTouchPoly(onPolyAfterTouch);


  //register as a client by sending any message
  //this is needed in this case, as the client will stay unkown to the dongle until the first message is sent.
  ESP_NOW_MIDI.sendControlChange(127, 127, 16);
}


void loop() {
}
