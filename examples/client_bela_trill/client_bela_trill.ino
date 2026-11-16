/*
  Read a Bela Trill Square (2D) or Trill Ring (1D + 2 buttons) capacitive touch
  sensor over I2C and send touch position and size as MIDI control changes over
  ESP-NOW. Optionally sends a Note On while touched and a Note Off on release.
  On the Ring, the two center buttons are sent as CCs (127 pressed / 0 released).

  Select the sensor with TRILL_TYPE in config.h. Setup and wiring: see README.md.

  Dependency: Arduino Library Manager — "Trill" by Bela
  (https://github.com/BelaPlatform/Trill-Arduino).

  Dongle: run examples/print_mac and set peerMacAddress below (or use addPeerFromString).
*/

#include "config.h"
#include "enomik_client.h"

#include <Trill.h>
#include <Wire.h>

#if TRILL_TYPE == TRILL_TYPE_RING
#define TRILL_DEVICE Trill::TRILL_RING
#define TRILL_NAME "Trill Ring"
#elif TRILL_TYPE == TRILL_TYPE_SQUARE
#define TRILL_DEVICE Trill::TRILL_SQUARE
#define TRILL_NAME "Trill Square"
#else
#error "TRILL_TYPE must be TRILL_TYPE_SQUARE or TRILL_TYPE_RING"
#endif

Trill _trill;

enomik::Client _client;

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = {0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C};

bool _trillOk = false;
bool _touching = false;
unsigned long _lastSendMs = 0;

// last sent values, -1 = nothing sent yet (only send on change)
int _lastX = -1;
int _lastY = -1;
int _lastSize = -1;
int _lastButton[2] = {-1, -1};

static uint8_t mapLocation(int raw, bool invert) {
  long v = constrain(map((long)raw, 0, TRILL_LOCATION_MAX, 0, 127), 0, 127);
  return (uint8_t)(invert ? 127 - v : v);
}

static uint8_t mapSize(int raw) {
  return (uint8_t)constrain(
      map((long)raw, TRILL_SIZE_MIN, TRILL_SIZE_MAX, 0, 127), 0, 127);
}

static void sendIfChanged(uint8_t cc, int value, int &last) {
  if (value == last) {
    return;
  }
  last = value;
  _client.sendControlChange(cc, (uint8_t)value, MIDI_CHANNEL);
}

static void handleTouch() {
  bool touching = _trill.getNumTouches() > 0;
#if TRILL_TYPE == TRILL_TYPE_SQUARE
  // Square reports vertical touches (touchLocation) and horizontal touches
  // (touchHorizontalLocation) separately; a touch needs both.
  touching = touching && _trill.getNumHorizontalTouches() > 0;
#endif

  if (touching) {
    // only the first touch is used
#if TRILL_TYPE == TRILL_TYPE_SQUARE
    int rawX = _trill.touchHorizontalLocation(0);
    int rawY = _trill.touchLocation(0);
    int rawSize = max(_trill.touchSize(0), _trill.touchHorizontalSize(0));
#else
    int rawX = _trill.touchLocation(0);
    int rawSize = _trill.touchSize(0);
#endif

#if TRILL_DEBUG
#if TRILL_TYPE == TRILL_TYPE_SQUARE
    Serial.printf("x=%d y=%d size=%d\n", rawX, rawY, rawSize);
#else
    Serial.printf("position=%d size=%d\n", rawX, rawSize);
#endif
#endif

    uint8_t size = mapSize(rawSize);

    // send position before Note On so receivers see the right value at note start
    sendIfChanged(CC_TRILL_X, mapLocation(rawX, TRILL_INVERT_X), _lastX);
#if TRILL_TYPE == TRILL_TYPE_SQUARE
    sendIfChanged(CC_TRILL_Y, mapLocation(rawY, TRILL_INVERT_Y), _lastY);
#endif
    sendIfChanged(CC_TRILL_SIZE, size, _lastSize);

#if ENABLE_TRILL_NOTE
    if (!_touching) {
      _client.sendNoteOn(TRILL_NOTE, max<uint8_t>(size, 1), MIDI_CHANNEL);
    }
#endif
  } else if (_touching) {
    sendIfChanged(CC_TRILL_SIZE, 0, _lastSize);
#if ENABLE_TRILL_NOTE
    _client.sendNoteOff(TRILL_NOTE, 0, MIDI_CHANNEL);
#endif
  }

  _touching = touching;
}

#if TRILL_TYPE == TRILL_TYPE_RING
static void handleButtons() {
  const uint8_t ccs[2] = {CC_TRILL_BUTTON_0, CC_TRILL_BUTTON_1};
  for (uint8_t b = 0; b < 2; b++) {
    int raw = _trill.getButtonValue(b);
#if TRILL_DEBUG
    if (raw > 0) {
      Serial.printf("button%u=%d\n", b, raw);
    }
#endif
    sendIfChanged(ccs[b], raw > TRILL_BUTTON_THRESHOLD ? 127 : 0, _lastButton[b]);
  }
}
#endif

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin();

  _client.setName("client_bela_trill"); // optional
  _client.begin();

  // default I2C address for the device type (Square 0x28, Ring 0x38)
  int ret = _trill.setup(TRILL_DEVICE);
  _trillOk = (ret == 0);
  if (!_trillOk) {
    Serial.print(F(TRILL_NAME " does not respond, error code: "));
    Serial.println(ret);
    return;
  }
  Serial.println(F(TRILL_NAME " is connected"));
}

void loop() {
  _client.loop();

  if (!_trillOk) {
    return;
  }

  unsigned long now = millis();
  if (now - _lastSendMs < MIDI_SEND_INTERVAL_MS) {
    return;
  }
  _lastSendMs = now;

  if (!_trill.read()) {
    return;
  }

  handleTouch();
#if TRILL_TYPE == TRILL_TYPE_RING
  handleButtons();
#endif
}
