/*
  Read NFC tags with a PN532 over I2C and send a MIDI message per tag over ESP-NOW.

  Dependency: Arduino Library Manager — "Adafruit PN532" (also installs Adafruit BusIO).
  https://github.com/adafruit/Adafruit-PN532

  Wiring: PN532 in I2C mode (DIP switches / jumpers: SEL0 = 1, SEL1 = 0), SDA / SCL
  to the board's I2C pins, VCC, GND. IRQ and RESET are optional, see config.h.
  Power the PN532 from 5V (VBUS), not 3V3: its RF field draws current peaks that a
  3V3 rail can't supply, the chip browns out and drops off the bus while the power
  LED stays lit. The I2C lines stay at 3.3 V, so SDA / SCL are safe on the ESP32.

  Behaviour:
    * In mapping mode, every tag that is placed or removed is reported via SysEx (UID).
      Outside mapping mode, tags only send their mapped MIDI message (no SysEx).
    * Mapping mode is switched on and off via SysEx. While it is on:
        1. place a tag on the reader
        2. send a MIDI message to this client (note on, note off, CC or program change)
      The message is stored for that tag (in flash, survives a reboot). Repeat for
      more tags. A tag that is mapped again gets the new message.
    * While mapping mode is off, placing a mapped tag sends its message. Removing a
      tag that sends a note on sends the matching note off (NFC_NOTE_OFF_ON_REMOVE).

  SysEx (non-commercial ID 0x7D; 7F 7F keeps it apart from enomik config SysEx):

    to this client:
      F0 7D 7F 7F 4E 00 F7                     mapping mode off
      F0 7D 7F 7F 4E 01 F7                     mapping mode on
      F0 7D 7F 7F 4E 02 F7                     delete all mappings
      F0 7D 7F 7F 4E 03 F7                     send all mappings (one 13 each, then 14)

    from this client:
      F0 7D 7F 7F 4E 10 <uid> F7               tag placed (mapping mode only)
      F0 7D 7F 7F 4E 11 <uid> F7               tag removed (mapping mode only)
      F0 7D 7F 7F 4E 12 <on> F7                mapping mode is now off (00) / on (01)
      F0 7D 7F 7F 4E 13 <uid> tt cc d1 d2 F7   tag mapped: tt = type (08 note off, 09 note on,
                                               0B CC, 0C program change), cc = channel 1..16
      F0 7D 7F 7F 4E 14 <count> F7             end of mapping list / all mappings deleted /
                                               mapping table full (count)

    <uid> = length n (4 or 7), then 2n nibbles, high nibble first.
            UID 04 A2 3B 1C -> 04 00 04 0A 02 03 0B 01 0C

  Over USB (HAS_USB_MIDI) the library also feeds every F0 7D ... message to its own
  config handler, which answers this SysEx with a BAD_VERSION error. Use ESP-NOW
  (via the dongle), or ignore that reply.
*/

#include "config.h"
#include "enomik_client.h"

#include <Adafruit_PN532.h>
#include <Preferences.h>
#include <Wire.h>

// Serial logging, see NFC_LOG_LEVEL in config.h. The level is a compile-time
// constant, so disabled log calls are removed by the compiler.
#define NFC_LOG_ERRORS 1
#define NFC_LOG_EVENTS 2
#define NFC_LOG_STATS 3
#define NFC_LOG(level, ...) do { if (NFC_LOG_LEVEL >= (level)) Serial.printf(__VA_ARGS__); } while (0)

enomik::Client _client;

Adafruit_PN532 nfc(PN532_IRQ_PIN, PN532_RESET_PIN, &Wire);
bool _nfcOk = false;

// --- SysEx protocol ---------------------------------------------------------------
static const uint8_t kSysExHeader[] = {0xF0, 0x7D, 0x7F, 0x7F, 0x4E};
static const size_t kSysExHeaderSize = sizeof(kSysExHeader);

enum NfcCommand : uint8_t {
  CMD_MAPPING_OFF = 0x00,
  CMD_MAPPING_ON = 0x01,
  CMD_CLEAR = 0x02,
  CMD_GET_MAPPINGS = 0x03,
  REPORT_TAG_PLACED = 0x10,
  REPORT_TAG_REMOVED = 0x11,
  REPORT_MAPPING_STATE = 0x12,
  REPORT_TAG_MAPPED = 0x13,
  REPORT_MAPPING_COUNT = 0x14,
};

// --- Tags and mappings --------------------------------------------------------------
static const uint8_t kMaxUidLength = 7; // ISO14443A: 4 or 7 bytes

struct Tag {
  uint8_t length = 0; // 0 = no tag
  uint8_t uid[kMaxUidLength] = {};

  bool equals(const Tag &other) const {
    return length == other.length && memcmp(uid, other.uid, length) == 0;
  }
};

struct Mapping {
  Tag tag;
  uint8_t type;    // MIDI status high nibble: 0x8, 0x9, 0xB, 0xC
  uint8_t channel; // 1..16
  uint8_t data1;
  uint8_t data2;
};

Mapping _mappings[NFC_MAX_MAPPINGS];
uint8_t _mappingCount = 0;

bool _mappingMode = false;
Tag _armedTag;   // mapping mode: tag waiting for its MIDI message
Tag _currentTag; // tag on the reader right now
uint8_t _misses = 0;

Preferences _prefs;
static const char *kPrefsNamespace = "client_nfc";

static void loadMappings() {
  _prefs.begin(kPrefsNamespace, true);
  _mappingCount = _prefs.getUChar("count", 0);
  if (_mappingCount > NFC_MAX_MAPPINGS ||
      _prefs.getBytes("maps", _mappings, sizeof(_mappings)) != sizeof(_mappings)) {
    _mappingCount = 0;
  }
  _prefs.end();
  NFC_LOG(NFC_LOG_EVENTS, "%u NFC mapping(s) loaded\n", _mappingCount);
}

static void saveMappings() {
  _prefs.begin(kPrefsNamespace, false);
  _prefs.putBytes("maps", _mappings, sizeof(_mappings));
  _prefs.putUChar("count", _mappingCount);
  _prefs.end();
}

static int findMapping(const Tag &tag) {
  for (uint8_t i = 0; i < _mappingCount; i++) {
    if (_mappings[i].tag.equals(tag)) return i;
  }
  return -1;
}

// --- SysEx out ----------------------------------------------------------------------
class SysExWriter {
public:
  explicit SysExWriter(uint8_t command) {
    memcpy(_data, kSysExHeader, kSysExHeaderSize);
    _length = kSysExHeaderSize;
    add(command);
  }

  void add(uint8_t value) { _data[_length++] = value & 0x7F; }

  void addTag(const Tag &tag) {
    add(tag.length);
    for (uint8_t i = 0; i < tag.length; i++) {
      add(tag.uid[i] >> 4);
      add(tag.uid[i] & 0x0F);
    }
  }

  void send() {
    _data[_length++] = 0xF7;
    _client.sendSysEx(_data, _length);
  }

private:
  uint8_t _data[32];
  uint16_t _length;
};

static void reportTag(uint8_t command, const Tag &tag) {
  SysExWriter msg(command);
  msg.addTag(tag);
  msg.send();
}

static void reportMappingState() {
  SysExWriter msg(REPORT_MAPPING_STATE);
  msg.add(_mappingMode ? 1 : 0);
  msg.send();
}

static void reportMappingCount() {
  SysExWriter msg(REPORT_MAPPING_COUNT);
  msg.add(_mappingCount);
  msg.send();
}

static void reportMapped(const Mapping &m) {
  SysExWriter msg(REPORT_TAG_MAPPED);
  msg.addTag(m.tag);
  msg.add(m.type);
  msg.add(m.channel);
  msg.add(m.data1);
  msg.add(m.data2);
  msg.send();
}

static void printTag(const char *label, const Tag &tag) {
  if (NFC_LOG_LEVEL < NFC_LOG_EVENTS) return;
  Serial.print(label);
  for (uint8_t i = 0; i < tag.length; i++) Serial.printf(" %02X", tag.uid[i]);
  Serial.println();
}

// --- Mapping ------------------------------------------------------------------------
static void learn(uint8_t type, uint8_t channel, uint8_t data1, uint8_t data2) {
  if (!_mappingMode || _armedTag.length == 0) return;

  int index = findMapping(_armedTag);
  if (index < 0) {
    if (_mappingCount >= NFC_MAX_MAPPINGS) {
      NFC_LOG(NFC_LOG_ERRORS, "NFC mapping table full\n");
      reportMappingCount();
      return;
    }
    index = _mappingCount++;
  }

  Mapping &m = _mappings[index];
  m.tag = _armedTag;
  m.type = type;
  m.channel = channel;
  m.data1 = data1;
  m.data2 = data2;
  saveMappings();

  printTag("mapped tag", m.tag);
  reportMapped(m);
  _armedTag = Tag(); // place the next tag to map another one
}

static void sendMapped(const Mapping &m, bool removed) {
  switch (m.type) {
    case 0x8:
      if (!removed) _client.sendNoteOff(m.data1, m.data2, m.channel);
      break;
    case 0x9:
      if (!removed) _client.sendNoteOn(m.data1, m.data2, m.channel);
      else if (NFC_NOTE_OFF_ON_REMOVE) _client.sendNoteOff(m.data1, 0, m.channel);
      break;
    case 0xB:
      if (!removed) _client.sendControlChange(m.data1, m.data2, m.channel);
      break;
    case 0xC:
      if (!removed) _client.sendProgramChange(m.data1, m.channel);
      break;
  }
}

// --- Tag events -----------------------------------------------------------------------
static void onTagPlaced(const Tag &tag) {
  printTag("tag placed", tag);

  if (_mappingMode) {
    reportTag(REPORT_TAG_PLACED, tag);
    _armedTag = tag;
    return;
  }
  const int index = findMapping(tag);
  if (index >= 0) sendMapped(_mappings[index], false);
}

static void onTagRemoved(const Tag &tag) {
  printTag("tag removed", tag);

  if (_mappingMode) {
    reportTag(REPORT_TAG_REMOVED, tag);
    return;
  }
  const int index = findMapping(tag);
  if (index >= 0) sendMapped(_mappings[index], true);
}

static void onTagSeen(const Tag &tag) {
  _misses = 0;
  if (_currentTag.equals(tag)) return;
  if (_currentTag.length) onTagRemoved(_currentTag); // swapped without a gap
  _currentTag = tag;
  onTagPlaced(tag);
}

static void onNoTagSeen() {
  if (_currentTag.length == 0 || ++_misses < NFC_REMOVE_MISSES) return;
  const Tag removed = _currentTag;
  _currentTag = Tag();
  _misses = 0;
  onTagRemoved(removed);
}

// --- PN532 setup ------------------------------------------------------------------------
static const uint8_t kPn532I2cAddress = PN532_I2C_ADDRESS; // 0x24, fixed in the chip
uint32_t _nfcRetryMs = 0;

static bool pn532Ready();

// Reads and drops a response the library leaves unread (setPassiveActivationRetries
// only checks the ACK). Otherwise it is still pending when the next command starts.
static void discardPn532Response(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while (millis() - startMs < timeoutMs) {
    if (pn532Ready()) {
      Wire.requestFrom(kPn532I2cAddress, (uint8_t)16);
      while (Wire.available()) Wire.read();
      return;
    }
    delay(1);
  }
}

static bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

static void printI2cScan() {
  uint8_t found = 0;
  Serial.print(F("  I2C devices found:"));
  for (uint8_t address = 1; address < 127; address++) {
    if (!i2cDevicePresent(address)) continue;
    Serial.printf(" 0x%02X", address);
    found++;
  }
  if (!found) Serial.print(F(" none"));
  Serial.println();
}

// Starts the PN532 and logs why it failed. The full report (pins, bus scan, hints)
// is printed on the first attempt; retries from loop() only print one line.
static bool initNfc(bool verbose) {
#if NFC_I2C_SDA_PIN >= 0 && NFC_I2C_SCL_PIN >= 0
  const int sdaPin = NFC_I2C_SDA_PIN, sclPin = NFC_I2C_SCL_PIN;
#else
  const int sdaPin = SDA, sclPin = SCL;
#endif
  if (verbose)
    NFC_LOG(NFC_LOG_ERRORS, "PN532: I2C on SDA %d, SCL %d, %lu Hz, address 0x%02X\n", sdaPin, sclPin,
            (unsigned long)NFC_I2C_CLOCK_HZ, kPn532I2cAddress);

  if (!nfc.begin()) {
    NFC_LOG(NFC_LOG_ERRORS, "PN532: I2C bus could not be started\n");
    return false;
  }
  Wire.setClock(NFC_I2C_CLOCK_HZ); // nfc.begin() calls Wire.begin() again
  const uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    const bool present = i2cDevicePresent(kPn532I2cAddress);
    NFC_LOG(NFC_LOG_ERRORS, "PN532 does not respond (address 0x%02X %s), retrying every %u ms\n", kPn532I2cAddress,
                  present ? "acknowledges" : "not found", (unsigned)NFC_RETRY_INTERVAL_MS);
    if (verbose && NFC_LOG_LEVEL >= NFC_LOG_ERRORS) {
      printI2cScan();
      if (present) {
        Serial.println(F("  The chip is on the bus but sends no firmware version: power-cycle the reader"));
        Serial.println(F("  (unplug it, not only the ESP32) and check its supply voltage."));
      } else {
        Serial.println(F("  Check: reader mode switches set to I2C (Elechouse V3: SW1 ON, SW2 OFF,"));
        Serial.println(F("  power-cycle after changing), SDA/SCL not swapped, VCC on 5V (3V3 browns"));
        Serial.println(F("  out, LED stays lit), NFC_I2C_SDA_PIN / NFC_I2C_SCL_PIN in config.h match."));
      }
    }
    return false;
  }
  NFC_LOG(NFC_LOG_ERRORS, "PN532 found: chip PN5%02lX, firmware %lu.%lu - waiting for tags\n",
                (unsigned long)(version >> 24) & 0xFF, (unsigned long)(version >> 16) & 0xFF,
                (unsigned long)(version >> 8) & 0xFF);

  // SAMConfig is the first command that switches the antenna on. A reader that
  // answers getFirmwareVersion but fails here usually browns out from the RF current.
  if (!nfc.SAMConfig()) {
    NFC_LOG(NFC_LOG_ERRORS, "PN532: SAMConfig failed (address 0x%02X %s) - the reader stops answering once\n",
                  kPn532I2cAddress, i2cDevicePresent(kPn532I2cAddress) ? "acknowledges" : "not found");
    NFC_LOG(NFC_LOG_ERRORS, "  the antenna turns on: check its supply (try 5 V on VCC, add 10-100 uF near the reader).\n");
    return false;
  }
  if (!nfc.setPassiveActivationRetries(0x01)) { // answer quickly when no tag is present
    NFC_LOG(NFC_LOG_ERRORS, "PN532: setPassiveActivationRetries failed\n");
    return false;
  }
  discardPn532Response(50);
  return true;
}

// --- PN532 polling ----------------------------------------------------------------------
// Non-blocking: start a detection, return to loop(), read the answer once the reader
// reports ready (status byte 0x01). Reading before that returns garbage and leaves
// the real answer to collide with the next command.
struct NfcStats {
  uint32_t polls = 0, tags = 0, noTag = 0, startFailed = 0, timeouts = 0;
} _nfcStats;

static bool pn532Ready() {
  if (Wire.requestFrom(kPn532I2cAddress, (uint8_t)1) != 1) return false;
  return Wire.read() == 0x01;
}

static void printNfcStats() {
#if NFC_LOG_LEVEL >= NFC_LOG_STATS
  static uint32_t lastMs = 0;
  if (millis() - lastMs < NFC_STATS_INTERVAL_MS) return;
  lastMs = millis();
  Serial.printf("NFC: %lu polls, %lu with tag, %lu without, %lu start failed, %lu timed out\n",
                (unsigned long)_nfcStats.polls, (unsigned long)_nfcStats.tags, (unsigned long)_nfcStats.noTag,
                (unsigned long)_nfcStats.startFailed, (unsigned long)_nfcStats.timeouts);
#endif
}

static void pollNfc() {
  static bool waiting = false;
  static uint32_t startedMs = 0;
  static uint8_t consecutiveStartFails = 0;
  const uint32_t now = millis();

  printNfcStats();

  if (!waiting) {
    if (now - startedMs < NFC_POLL_INTERVAL_MS) return;
    startedMs = now;
    _nfcStats.polls++;
    waiting = nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
    if (!waiting) {
      _nfcStats.startFailed++;
      onNoTagSeen();
      // The reader stopped answering: start it again from loop().
      if (++consecutiveStartFails >= NFC_MAX_START_FAILS) {
        consecutiveStartFails = 0;
        NFC_LOG(NFC_LOG_ERRORS, "PN532: %u detections in a row failed (address 0x%02X %s), restarting the reader\n",
                      (unsigned)NFC_MAX_START_FAILS, kPn532I2cAddress,
                      i2cDevicePresent(kPn532I2cAddress) ? "acknowledges" : "not found");
        _nfcOk = false;
      }
    } else {
      consecutiveStartFails = 0;
    }
    return;
  }

  if (!pn532Ready()) {
    if (now - startedMs < NFC_RESPONSE_TIMEOUT_MS) return;
    waiting = false;
    _nfcStats.timeouts++;
    onNoTagSeen();
    return;
  }
  waiting = false;

  // The library copies as many UID bytes as the reader reports, up to 255.
  static uint8_t uid[256];
  uint8_t uidLength = 0;
  if (nfc.readDetectedPassiveTargetID(uid, &uidLength) && uidLength > 0 && uidLength <= kMaxUidLength) {
    _nfcStats.tags++;
    Tag tag;
    tag.length = uidLength;
    memcpy(tag.uid, uid, uidLength);
    onTagSeen(tag);
  } else {
    _nfcStats.noTag++;
    onNoTagSeen();
  }
}

// --- MIDI in ------------------------------------------------------------------------------
void onNoteOn(byte channel, byte note, byte velocity) { learn(0x9, channel, note, velocity); }
void onNoteOff(byte channel, byte note, byte velocity) { learn(0x8, channel, note, velocity); }
void onControlChange(byte channel, byte control, byte value) { learn(0xB, channel, control, value); }
void onProgramChange(byte channel, byte program) { learn(0xC, channel, program, 0); }

void onSysEx(uint8_t *data, unsigned int length) {
  if (length < kSysExHeaderSize + 2 || memcmp(data, kSysExHeader, kSysExHeaderSize) != 0) return;

  switch (data[kSysExHeaderSize]) {
    case CMD_MAPPING_OFF:
    case CMD_MAPPING_ON:
      _mappingMode = data[kSysExHeaderSize] == CMD_MAPPING_ON;
      _armedTag = Tag();
      NFC_LOG(NFC_LOG_EVENTS, "mapping mode %s\n", _mappingMode ? "on" : "off");
      reportMappingState();
      break;
    case CMD_CLEAR:
      _mappingCount = 0;
      _armedTag = Tag();
      saveMappings();
      NFC_LOG(NFC_LOG_EVENTS, "all NFC mappings deleted\n");
      reportMappingCount();
      break;
    case CMD_GET_MAPPINGS:
      for (uint8_t i = 0; i < _mappingCount; i++) reportMapped(_mappings[i]);
      reportMappingCount();
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

#if NFC_I2C_SDA_PIN >= 0 && NFC_I2C_SCL_PIN >= 0
  Wire.begin(NFC_I2C_SDA_PIN, NFC_I2C_SCL_PIN);
#else
  Wire.begin();
#endif
  Wire.setClock(NFC_I2C_CLOCK_HZ);

  loadMappings();

  // Before _client.begin(): it re-attaches USB, and serial output printed while
  // the port reconnects is lost.
  _nfcOk = initNfc(true);

  _client.setName("client_nfc"); // optional
  _client.begin();
  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleNoteOff(onNoteOff);
  _client.setHandleControlChange(onControlChange);
  _client.setHandleProgramChange(onProgramChange);
  _client.setHandleSysEx(onSysEx);
}

void loop() {
  _client.loop();
  if (_nfcOk) {
    pollNfc();
  } else if (millis() - _nfcRetryMs >= NFC_RETRY_INTERVAL_MS) {
    _nfcRetryMs = millis();
    _nfcOk = initNfc(false);
  }
}
