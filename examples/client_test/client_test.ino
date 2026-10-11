// client_test - deterministic device-under-test for the test wizard.
//
// This is NOT a product sketch. It is a fixed-behaviour firmware that the
// scripts/wizard harness drives step by step:
//
//   Phase 1 (USB):     wizard talks to this board directly over USB MIDI
//                      (SysEx configuration, echo, burst, inputs)
//   Phase 2 (ESP-NOW): wizard talks to the dongle; this board is reached
//                      wirelessly. Same echo, burst and input checks.
//
// Contract relied on by the wizard (MIDI only — no side-channel protocol):
//   * channel-voice messages on TEST_ECHO_CHANNEL are echoed back verbatim
//   * system real-time and common messages are echoed back on any channel
//   * messages on TEST_FAKE_CHANNEL drive fake inputs instead of the pins:
//       CC n = v        analog pin n reads level v (0..127)
//       note on n       digital pin n reads LOW (pressed)
//       note off n      digital pin n reads HIGH (released)
//       program change  all pins read the hardware again
//   * program change p on TEST_API_TRIGGER_CHANNEL makes this board send API
//     case p with the enomik::Client send functions (see runApiCase())
//   * the library's ANNOUNCE heartbeat runs every TEST_HEARTBEAT_INTERVAL_MS
//     so the dongle discovers this client quickly once its MAC is stored

#include "config.h"
#include "enomik_client.h"

enomik::Client _client;

static const byte kTestChannel = TEST_ECHO_CHANNEL;
static const byte kFakeChannel = TEST_FAKE_CHANNEL;
static const byte kApiTriggerChannel = TEST_API_TRIGGER_CHANNEL;
static const byte kApiChannel = TEST_API_CHANNEL;

// --- Fake inputs ---------------------------------------------------------------
static const int kNotFaked = -1;
static int _fakeAnalog[TEST_FAKE_PIN_COUNT];
static int _fakeDigital[TEST_FAKE_PIN_COUNT];

static void clearFakes() {
  for (int i = 0; i < TEST_FAKE_PIN_COUNT; i++) {
    _fakeAnalog[i] = kNotFaked;
    _fakeDigital[i] = kNotFaked;
  }
}

// ADC value in the middle of the range that maps back to this 7-bit level.
static int adcForLevel(int level) {
  if (level >= 127) return ADC_MAX_VALUE;
  return ((2 * level + 1) * ADC_MAX_VALUE) / 254;
}

static int readAnalog(uint8_t pin) {
  if (pin < TEST_FAKE_PIN_COUNT && _fakeAnalog[pin] != kNotFaked) return _fakeAnalog[pin];
  return analogRead(pin);
}

static int readDigital(uint8_t pin) {
  if (pin < TEST_FAKE_PIN_COUNT && _fakeDigital[pin] != kNotFaked) return _fakeDigital[pin];
  return digitalRead(pin);
}

static void setFakeDigital(byte pin, bool pressed) {
  if (pin < TEST_FAKE_PIN_COUNT) _fakeDigital[pin] = pressed ? LOW : HIGH;
}

// --- API sends ------------------------------------------------------------------
// Keep in sync with API_CASES in scripts/wizard/cases.py.
static const uint8_t kSysExShort[] = {0xF0, 0x7D, 0x7F, 0x7F, 0x01, 0x02, 0x03, 0xF7};
static const uint8_t kSysExLong[] = {0xF0, 0x7D, 0x7F, 0x7F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                     0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0xF7};

static void runApiCase(byte index) {
  switch (index) {
    case 0: _client.sendNoteOn(60, 100, kApiChannel); break;
    case 1: _client.sendNoteOff(60, 0, kApiChannel); break;
    case 2: _client.sendControlChange(20, 64, kApiChannel); break;
    case 3: _client.sendProgramChange(7, kApiChannel); break;
    case 4: _client.sendPitchBend(-8192, kApiChannel); break;
    case 5: _client.sendPitchBend(0, kApiChannel); break;
    case 6: _client.sendPitchBend(8191, kApiChannel); break;
    case 7: _client.sendAfterTouch(50, kApiChannel); break;
    case 8: _client.sendPolyAfterTouch(60, 70, kApiChannel); break;
    case 9: _client.sendClock(); break;
    case 10: _client.sendStart(); break;
    case 11: _client.sendContinue(); break;
    case 12: _client.sendStop(); break;
    case 13: _client.sendSongPosition(1234); break;
    case 14: _client.sendSongSelect(3); break;
    case 15: _client.sendSysEx(kSysExShort, sizeof(kSysExShort)); break;
    case 16: _client.sendSysEx(kSysExLong, sizeof(kSysExLong)); break;
  }
}

// --- Echo handlers: mirror back only on the test channel ---------------------
void onNoteOn(byte channel, byte note, byte velocity) {
  if (channel == kFakeChannel) setFakeDigital(note, velocity > 0);
  if (channel == kTestChannel) _client.sendNoteOn(note, velocity, channel);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  if (channel == kFakeChannel) setFakeDigital(note, false);
  if (channel == kTestChannel) _client.sendNoteOff(note, velocity, channel);
}

void onControlChange(byte channel, byte control, byte value) {
  if (channel == kFakeChannel && control < TEST_FAKE_PIN_COUNT) _fakeAnalog[control] = adcForLevel(value);
  if (channel == kTestChannel) _client.sendControlChange(control, value, channel);
}

void onProgramChange(byte channel, byte program) {
  if (channel == kFakeChannel) clearFakes();
  if (channel == kApiTriggerChannel) runApiCase(program);
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

// --- System messages: echoed regardless of channel -----------------------------
void onClock() { _client.sendClock(); }
void onStart() { _client.sendStart(); }
void onContinue() { _client.sendContinue(); }
void onStop() { _client.sendStop(); }
void onSongPosition(uint16_t position) { _client.sendSongPosition(position); }
void onSongSelect(byte song) { _client.sendSongSelect(song); }

void setup() {
  clearFakes();

  _client.setName("client_test"); // optional
  _client.setHeartbeatInterval(TEST_HEARTBEAT_INTERVAL_MS);
  _client.begin();
  _client.io.setAnalogReader(readAnalog);
  _client.io.setDigitalReader(readDigital);

  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleNoteOff(onNoteOff);
  _client.setHandleControlChange(onControlChange);
  _client.setHandleProgramChange(onProgramChange);
  _client.setHandlePitchBend(onPitchBend);
  _client.setHandleAfterTouchChannel(onAfterTouch);
  _client.setHandleAfterTouchPoly(onPolyAfterTouch);
  _client.setHandleClock(onClock);
  _client.setHandleStart(onStart);
  _client.setHandleContinue(onContinue);
  _client.setHandleStop(onStop);
  _client.setHandleSongPosition(onSongPosition);
  _client.setHandleSongSelect(onSongSelect);
}

void loop() {
  _client.loop();
}
