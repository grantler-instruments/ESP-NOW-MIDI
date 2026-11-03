/*
  Read NFC tags with a PN532 over I2C and send a MIDI message per tag over ESP-NOW.

  Dependency: Arduino Library Manager — "Adafruit PN532" (also installs Adafruit BusIO).
  https://github.com/adafruit/Adafruit-PN532

  Wiring: PN532 in I2C mode (DIP switches / jumpers: SEL0 = 1, SEL1 = 0), SDA / SCL
  to the board's I2C pins, VCC, GND. IRQ and RESET are optional, see config.h.

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
  Serial.printf("%u NFC mapping(s) loaded\n", _mappingCount);
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
      Serial.println(F("NFC mapping table full"));
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

// --- PN532 polling ----------------------------------------------------------------------
// Non-blocking: start a detection, return to loop(), read the answer later.
static void pollNfc() {
  static bool waiting = false;
  static uint32_t startedMs = 0;
  const uint32_t now = millis();

  if (!waiting) {
    if (now - startedMs < NFC_POLL_INTERVAL_MS) return;
    startedMs = now;
    waiting = nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A);
    if (!waiting) onNoTagSeen();
    return;
  }

  if (now - startedMs < NFC_RESPONSE_WAIT_MS) return;
  waiting = false;

  // The library copies as many UID bytes as the reader reports, up to 255.
  static uint8_t uid[256];
  uint8_t uidLength = 0;
  if (nfc.readDetectedPassiveTargetID(uid, &uidLength) && uidLength > 0 && uidLength <= kMaxUidLength) {
    Tag tag;
    tag.length = uidLength;
    memcpy(tag.uid, uid, uidLength);
    onTagSeen(tag);
  } else {
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
      Serial.printf("mapping mode %s\n", _mappingMode ? "on" : "off");
      reportMappingState();
      break;
    case CMD_CLEAR:
      _mappingCount = 0;
      _armedTag = Tag();
      saveMappings();
      Serial.println(F("all NFC mappings deleted"));
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

  loadMappings();

  _client.setName("client_nfc"); // optional
  _client.begin();
  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleNoteOff(onNoteOff);
  _client.setHandleControlChange(onControlChange);
  _client.setHandleProgramChange(onProgramChange);
  _client.setHandleSysEx(onSysEx);

  _nfcOk = nfc.begin();
  const uint32_t version = _nfcOk ? nfc.getFirmwareVersion() : 0;
  if (!version) {
    _nfcOk = false;
    Serial.println(F("PN532 does not respond"));
    return;
  }
  Serial.printf("PN532 firmware %lu.%lu\n", (unsigned long)(version >> 16) & 0xFF,
                (unsigned long)(version >> 8) & 0xFF);

  nfc.SAMConfig();
  nfc.setPassiveActivationRetries(0x01); // answer quickly when no tag is present
}

void loop() {
  _client.loop();
  if (_nfcOk) pollNfc();
}
