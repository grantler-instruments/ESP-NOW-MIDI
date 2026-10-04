#include "enomik_client.h"

// on the dongle: run the print_mac firmware and paste it here
// uint8_t peerMacAddress[6] = { 0x48, 0x27, 0xE2, 0x47, 0x3D, 0x74 };
uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
enomik::Client _client;
byte channel = 1;

// Heartbeat: the onboard LED (GPIO 15 on the LOLIN S2 Mini) is on while a
// round of messages is sent and off during the pause, so it flashes about
// every 2.6 s. If it stops flashing (stuck on or off), the loop has hung.
#ifdef LED_BUILTIN
#define HEARTBEAT_LED LED_BUILTIN
#endif


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

void onPitchBend(byte channel, uint16_t value) {
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
#ifdef HEARTBEAT_LED
  pinMode(HEARTBEAT_LED, OUTPUT);
#endif
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

// One message every 100 ms, then a 2 s pause. No delay(): _client.loop() keeps
// running, so received MIDI is handled right away.
const unsigned long kStepMs = 100;
const unsigned long kPauseMs = 2000;
const int kSteps = 7;
int _step = 0;
unsigned long _nextStepMs = 0;

bool sendStep(int step) {
  switch (step) {
    case 0: return _client.sendNoteOn(60, 127, channel);
    case 1: return _client.sendNoteOff(60, 0, channel);
    case 2: return _client.sendControlChange(1, 127, channel);
    case 3: return _client.sendControlChange(1, 0, channel);
    case 4: return _client.sendPitchBend(-8192, channel);
    case 5: return _client.sendPitchBend(0, channel);
    default: return _client.sendPitchBend(8191, channel);
  }
}

void loop() {
  _client.loop();

  const unsigned long now = millis();
  if ((long)(now - _nextStepMs) < 0) {
    return;
  }
#ifdef HEARTBEAT_LED
  digitalWrite(HEARTBEAT_LED, _step < kSteps - 1 ? HIGH : LOW);
#endif
  if (!sendStep(_step)) {
    Serial.println("Error sending the data");
  }
  _step = (_step + 1) % kSteps;
  _nextStepMs = now + (_step == 0 ? kPauseMs : kStepMs);
}
