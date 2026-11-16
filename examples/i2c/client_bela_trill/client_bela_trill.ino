/*
  Read a Bela Trill capacitive touch sensor over I2C and send it as MIDI over
  ESP-NOW. The sensor type is detected automatically:

  - Bar, Ring, Flex (1D): position and touch size as CCs
  - Square, Hex (2D): X / Y position and touch size as CCs
  - Ring: also the two center buttons as CCs (127 pressed / 0 released)
  - Bar, Square, Ring, Hex, Flex: optional Note On while touched, Note Off on release
  - Craft: each of the 30 pads sends its own Note On / Note Off

  Settings: config.h. Wiring and setup: README.md.

  Dependency: Arduino Library Manager — "Trill" by Bela
  (https://github.com/BelaPlatform/Trill-Arduino).

  Dongle: run examples/print_mac and set peerMacAddress below (or use addPeerFromString).
*/

#include "config.h"
#include "enomik_client.h"

#include <Trill.h>
#include <Wire.h>

Trill _trill;

enomik::Client _client;

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = {0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C};

Trill::Device _device = Trill::TRILL_NONE;
unsigned long _lastScanMs = 0;
unsigned long _lastSendMs = 0;

// position range for the detected sensor (see config.h)
int _locationMaxX = 0;
int _locationMaxY = 0;

bool _touching = false;

// last sent values, -1 = nothing sent yet (only send on change)
int _lastX = -1;
int _lastY = -1;
int _lastSize = -1;
int _lastButton[2] = {-1, -1};

// Craft: one bit per pad, set while the pad is touched
static const uint8_t kCraftPads = 30;
uint32_t _craftPads = 0;

static uint8_t mapLocation(int raw, int max, bool invert) {
  long v = constrain(map((long)raw, 0, max, 0, 127), 0, 127);
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

// Find a Trill on the bus. Returns its address, or 0 if none answers.
static uint8_t findTrill(Trill::Device &device) {
#if TRILL_I2C_ADDRESS
  device = Trill::probe(TRILL_I2C_ADDRESS);
  return device == Trill::TRILL_NONE ? 0 : TRILL_I2C_ADDRESS;
#else
  // Same range as Bela's detect-all-devices example: every Trill's default
  // address and the ones selectable with its solder jumpers.
  for (uint8_t address = 0x20; address <= 0x50; address++) {
    device = Trill::probe(address);
    if (device != Trill::TRILL_NONE && device != Trill::TRILL_UNKNOWN) {
      return address;
    }
  }
  device = Trill::TRILL_NONE;
  return 0;
#endif
}

static bool setupTrill() {
  Trill::Device device;
  uint8_t address = findTrill(device);
  if (address == 0) {
    Serial.println(F("No Trill sensor found, check wiring (see README.md)"));
    return false;
  }

  int ret = _trill.setup(device, address);
  if (ret != 0) {
    Serial.printf("Trill %s at 0x%02X: setup failed, error code %d\n",
                  Trill::getNameFromDevice(device), address, ret);
    return false;
  }

  switch (device) {
  case Trill::TRILL_BAR:
    _locationMaxX = TRILL_BAR_LOCATION_MAX;
    break;
  case Trill::TRILL_RING:
    _locationMaxX = TRILL_RING_LOCATION_MAX;
    break;
  case Trill::TRILL_SQUARE:
    _locationMaxX = TRILL_SQUARE_LOCATION_MAX;
    _locationMaxY = TRILL_SQUARE_LOCATION_MAX;
    break;
  case Trill::TRILL_HEX:
    _locationMaxX = TRILL_HEX_X_LOCATION_MAX;
    _locationMaxY = TRILL_HEX_Y_LOCATION_MAX;
    break;
  case Trill::TRILL_FLEX:
    _locationMaxX = TRILL_FLEX_LOCATION_MAX;
    // Flex starts in differential mode; use it as a slider instead
    // (same settings as Bela's flex-print-slider example).
    _trill.setMode(Trill::CENTROID);
    delay(Trill::interCommandDelay);
    _trill.setPrescaler(TRILL_FLEX_PRESCALER);
    delay(Trill::interCommandDelay);
    _trill.setNoiseThreshold(TRILL_FLEX_NOISE_THRESHOLD);
    delay(Trill::interCommandDelay);
    _trill.updateBaseline();
    delay(Trill::interCommandDelay);
    break;
  case Trill::TRILL_CRAFT:
    break; // stays in differential mode, read per pad
  default:
    Serial.printf("Trill at 0x%02X: unsupported device type %d\n", address, (int)device);
    return false;
  }

  _device = device;
  Serial.printf("Trill %s found at 0x%02X\n", Trill::getNameFromDevice(device), address);
  return true;
}

static void handleTouch() {
  bool is2D = _trill.is2D();
  bool touching = _trill.getNumTouches() > 0;
  if (is2D) {
    // 2D sensors report vertical touches (touchLocation) and horizontal
    // touches (touchHorizontalLocation) separately; a touch needs both.
    touching = touching && _trill.getNumHorizontalTouches() > 0;
  }

  if (touching) {
    // only the first touch is used
    int rawX, rawY = 0, rawSize;
    if (is2D) {
      rawX = _trill.touchHorizontalLocation(0);
      rawY = _trill.touchLocation(0);
      rawSize = max(_trill.touchSize(0), _trill.touchHorizontalSize(0));
    } else {
      rawX = _trill.touchLocation(0);
      rawSize = _trill.touchSize(0);
    }

#if TRILL_DEBUG
    if (is2D) {
      Serial.printf("x=%d y=%d size=%d\n", rawX, rawY, rawSize);
    } else {
      Serial.printf("position=%d size=%d\n", rawX, rawSize);
    }
#endif

    uint8_t size = mapSize(rawSize);

    // send position before Note On so receivers see the right value at note start
    sendIfChanged(CC_TRILL_X, mapLocation(rawX, _locationMaxX, TRILL_INVERT_X), _lastX);
    if (is2D) {
      sendIfChanged(CC_TRILL_Y, mapLocation(rawY, _locationMaxY, TRILL_INVERT_Y), _lastY);
    }
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

static void handleRingButtons() {
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

static void handleCraft() {
  if (!_trill.requestRawData()) {
    return;
  }
  for (uint8_t pad = 0; pad < kCraftPads && _trill.rawDataAvailable() > 0; pad++) {
    int raw = _trill.rawDataRead();
#if TRILL_DEBUG
    if (raw > 0) {
      Serial.printf("pad%u=%d ", pad, raw);
    }
#endif
    bool touched = raw > TRILL_CRAFT_THRESHOLD;
    bool wasTouched = _craftPads & (1UL << pad);
    if (touched == wasTouched) {
      continue;
    }
    uint8_t note = TRILL_CRAFT_NOTE_BASE + pad;
    if (touched) {
      _craftPads |= (1UL << pad);
      _client.sendNoteOn(note, TRILL_CRAFT_VELOCITY, MIDI_CHANNEL);
    } else {
      _craftPads &= ~(1UL << pad);
      _client.sendNoteOff(note, 0, MIDI_CHANNEL);
    }
  }
  // drain anything left so the next request starts clean
  while (_trill.rawDataAvailable() > 0) {
    _trill.rawDataRead();
  }
#if TRILL_DEBUG
  Serial.println();
#endif
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin();

  _client.setName("client_bela_trill"); // optional
  _client.begin();

  setupTrill();
  _lastScanMs = millis();
}

void loop() {
  _client.loop();

  unsigned long now = millis();

  if (_device == Trill::TRILL_NONE) {
    if (now - _lastScanMs >= TRILL_RETRY_INTERVAL_MS) {
      _lastScanMs = now;
      setupTrill();
    }
    return;
  }

  if (now - _lastSendMs < MIDI_SEND_INTERVAL_MS) {
    return;
  }
  _lastSendMs = now;

  if (_device == Trill::TRILL_CRAFT) {
    handleCraft();
    return;
  }

  if (!_trill.read()) {
    return;
  }

  handleTouch();
  if (_device == Trill::TRILL_RING) {
    handleRingButtons();
  }
}
