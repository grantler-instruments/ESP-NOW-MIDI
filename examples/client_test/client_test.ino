// client_test - deterministic device-under-test for the test wizard.
//
// This is NOT a product sketch. It is a fixed-behaviour firmware that the
// scripts/wizard harness drives step by step:
//
//   Phase 1 (USB):     wizard talks to this board directly over USB MIDI
//                      - GET_MAC SysEx round-trip (proves SysEx both ways)
//                      - SET_PIN_CONFIG for the two button pins
//                      - echo test on TEST_ECHO_CHANNEL
//                      - ADD_PEER with the dongle MAC (persisted)
//                      - physical button presses -> notes
//
//   Phase 2 (ESP-NOW): wizard talks to the dongle; this board is reached
//                      wirelessly. Same echo + button checks over the air.
//
// Contract relied on by the wizard (MIDI only — no side-channel protocol):
//   * channel-voice messages on TEST_ECHO_CHANNEL are echoed back verbatim
//   * a registration handshake (CC 127 on ch 16) is sent periodically so the
//     dongle can auto-discover this client once its MAC is known

#include "config.h"
#include "enomik_client.h"

enomik::Client _client;

static const byte kTestChannel = TEST_ECHO_CHANNEL;
static unsigned long _lastHandshakeMs = 0;

// --- Echo handlers: mirror back only on the test channel ---------------------
void onNoteOn(byte channel, byte note, byte velocity) {
  if (channel == kTestChannel) _client.sendNoteOn(note, velocity, channel);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  if (channel == kTestChannel) _client.sendNoteOff(note, velocity, channel);
}

void onControlChange(byte channel, byte control, byte value) {
  if (channel == kTestChannel) _client.sendControlChange(control, value, channel);
}

void onProgramChange(byte channel, byte program) {
  if (channel == kTestChannel) _client.sendProgramChange(program, channel);
}

void onPitchBend(byte channel, int value) {
  if (channel == kTestChannel) _client.sendPitchBend(value, channel);
}

void onAfterTouch(byte channel, byte pressure) {
  if (channel == kTestChannel) _client.sendAfterTouch(pressure, channel);
}

void onPolyAfterTouch(byte channel, byte note, byte pressure) {
  if (channel == kTestChannel) _client.sendPolyAfterTouch(note, pressure, channel);
}

static void maybeHandshake() {
  unsigned long now = millis();
  if (now - _lastHandshakeMs < TEST_HANDSHAKE_INTERVAL_MS) return;
  _lastHandshakeMs = now;

  // Only meaningful once we know at least one peer (the dongle). Sending a
  // message lets the dongle auto-discover this client so it can route host
  // traffic back to us in Phase 2.
  if (_client.getPeerCount() > 0) {
    _client.sendControlChange(127, 127, 16);
  }
}

void setup() {
  _client.begin();

  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleNoteOff(onNoteOff);
  _client.setHandleControlChange(onControlChange);
  _client.setHandleProgramChange(onProgramChange);
  _client.setHandlePitchBend(onPitchBend);
  _client.setHandleAfterTouchChannel(onAfterTouch);
  _client.setHandleAfterTouchPoly(onPolyAfterTouch);
}

void loop() {
  _client.loop();
  maybeHandshake();
}
