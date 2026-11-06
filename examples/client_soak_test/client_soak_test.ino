// client_soak_test - long-run test for the ESP-NOW send path.
//
// Sends MIDI to the dongle for hours and watches whether the ESP-NOW driver
// keeps confirming sends. Made to catch the hang where esp_now_send() refuses
// every send (ESP_ERR_ESPNOW_NO_MEM), no send confirmations come back and only
// a reboot helps.
//
// What it does:
//   * sends MIDI in one of three load profiles (see SOAK_PROFILE below)
//   * optionally adds peers that do not exist, so every message goes to
//     several peers and waits for their missing ACKs (the multi-peer case)
//   * prints one [soak] line per minute with the library's send counters
//   * saves the first stall to flash (NVS), plus a checkpoint every 10 min,
//     so the result survives a reboot or a power cycle
//
// Stall = sends were attempted, but no confirmation (delivered or not) came
// back for SOAK_STALL_MS. The library restarts ESP-NOW after
// ESP_NOW_MIDI_STALL_MS; the [soak] line shows those restarts too.
//
// Serial commands (115200): p = print now, c = clear saved results,
//                           l / h / b = switch profile, ? = help
//
// LED (LED_BUILTIN): short blink every 2 s = healthy, fast blink = stalled
// right now, double blink = healthy, but a stall was saved since the last 'c'.
//
// Log both boards to a file on the computer with soak_logger.py (see README).

#include "enomik_client.h"
#include <Preferences.h>
#include <esp_system.h>

// --- Settings -------------------------------------------------------------------

// The dongle's MAC (run the print_mac example on it)
uint8_t dongleMac[6] = {0x48, 0xF6, 0xEE, 0x7A, 0x1C, 0xC8};

// Load profile at boot:
//   SOAK_LOW    7 messages 100 ms apart, then 2 s pause (the original hang sketch)
//   SOAK_HIGH   about SOAK_HIGH_RATE messages per second, without pause
//   SOAK_BURSTS SOAK_LOW, with SOAK_BURST_S seconds of SOAK_HIGH every 10 min
#define SOAK_LOW 0
#define SOAK_HIGH 1
#define SOAK_BURSTS 2
#ifndef SOAK_PROFILE
#define SOAK_PROFILE SOAK_BURSTS
#endif
#define SOAK_HIGH_RATE 350
#define SOAK_BURST_S 30
#define SOAK_BURST_EVERY_MS (10UL * 60UL * 1000UL)

// Peers that do not exist, added on top of the dongle (0..3). Each message is
// then sent to 1 + SOAK_EXTRA_PEERS peers, and the missing ones are retried
// by the radio until it gives up. Not stored, gone after a reboot.
#ifndef SOAK_EXTRA_PEERS
#define SOAK_EXTRA_PEERS 0
#endif

// Pins configured on this board (SysEx / web editor) also send MIDI, and an
// unconnected analog pin sends hundreds of messages per second. 0 = the pins
// read as idle, so only this sketch sends; 1 = they work as usual.
#ifndef SOAK_KEEP_INPUTS
#define SOAK_KEEP_INPUTS 0
#endif

#define SOAK_CHANNEL 1

// Note ons received on this channel with velocity 99 are sent back over
// ESP-NOW, so a computer can measure round trips through the dongle while the
// load runs (the Client does not forward received MIDI to USB). 0 = off.
#ifndef SOAK_ECHO_CHANNEL
#define SOAK_ECHO_CHANNEL 10
#endif
#define SOAK_REPORT_MS 60000UL
#define SOAK_CHECKPOINT_MS (10UL * 60UL * 1000UL)
#define SOAK_STALL_MS 3000UL

// --------------------------------------------------------------------------------

enomik::Client _client;

static const uint8_t kExtraPeers[3][6] = {
    {0x02, 0x50, 0x41, 0x4B, 0x00, 0x01},
    {0x02, 0x50, 0x41, 0x4B, 0x00, 0x02},
    {0x02, 0x50, 0x41, 0x4B, 0x00, 0x03},
};

static int _profile = SOAK_PROFILE;

// Own counters, next to the library's EspNowSendStats
static uint32_t _attempts = 0;
static uint32_t _accepted = 0;

// Stall tracking
static uint32_t _lastConfirms = 0;
static uint32_t _firstUnconfirmedMs = 0; // first attempt after the last confirmation
static bool _waitingForConfirm = false;
static bool _stalled = false;
static uint32_t _stallStartMs = 0;
static uint32_t _stallsThisBoot = 0;
static uint32_t _longestGapMs = 0; // longest wait for a confirmation while sending
static uint32_t _lastRestarts = 0;

// --- Saved results (NVS) ----------------------------------------------------------

struct SoakRecord
{
  uint32_t magic;
  uint32_t boot;
  uint32_t uptimeS;
  uint8_t profile;
  uint8_t extraPeers;
  uint8_t resetReason; // why this boot started (esp_reset_reason_t)
  uint32_t attempts, accepted;
  uint32_t sent, failed, delivered, notDelivered, throttled, skipped, restarts, wifiRestarts;
  uint32_t dropped, replaced, queuePeak;
  int32_t lastError;
  uint32_t stalls;
  uint32_t longestGapMs;
  uint32_t freeHeap, minFreeHeap;
};

static const uint32_t kRecordMagic = 0x534F4B34; // "SOK4"
static const char *kNamespace = "soak";

static uint32_t _boot = 0;
static esp_reset_reason_t _resetReason = ESP_RST_UNKNOWN;
static SoakRecord _firstStall = {};
static SoakRecord _checkpoint = {};
static uint32_t _savedStalls = 0; // all stalls since the last 'c', over all boots

static bool loadRecord(Preferences &prefs, const char *key, SoakRecord &out)
{
  if (prefs.getBytes(key, &out, sizeof(out)) != sizeof(out) || out.magic != kRecordMagic)
  {
    out.magic = 0;
    return false;
  }
  return true;
}

static void saveRecord(const char *key, const SoakRecord &record)
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false))
  {
    prefs.putBytes(key, &record, sizeof(record));
    prefs.end();
  }
}

static void saveStallCount()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false))
  {
    prefs.putUInt("stalls", _savedStalls);
    prefs.end();
  }
}

static void loadSaved()
{
  Preferences prefs;
  if (!prefs.begin(kNamespace, false))
  {
    return;
  }
  _boot = prefs.getUInt("boot", 0) + 1;
  prefs.putUInt("boot", _boot);
  _savedStalls = prefs.getUInt("stalls", 0);
  loadRecord(prefs, "first", _firstStall);
  loadRecord(prefs, "check", _checkpoint);
  prefs.end();
}

static void clearSaved()
{
  Preferences prefs;
  if (prefs.begin(kNamespace, false))
  {
    prefs.clear();
    prefs.putUInt("boot", _boot);
    prefs.end();
  }
  _firstStall.magic = 0;
  _checkpoint.magic = 0;
  _savedStalls = 0;
}

static void fillRecord(SoakRecord &r)
{
  const EspNowSendStats s = _client.getSendStats();
  r.magic = kRecordMagic;
  r.boot = _boot;
  r.uptimeS = millis() / 1000;
  r.profile = static_cast<uint8_t>(_profile);
  r.extraPeers = SOAK_EXTRA_PEERS;
  r.resetReason = static_cast<uint8_t>(_resetReason);
  r.attempts = _attempts;
  r.accepted = _accepted;
  r.sent = s.sent;
  r.failed = s.failed;
  r.delivered = s.delivered;
  r.notDelivered = s.notDelivered;
  r.throttled = s.throttled;
  r.skipped = s.skipped;
  r.restarts = s.restarts;
  r.wifiRestarts = s.wifiRestarts;
  r.dropped = s.dropped;
  r.replaced = s.replaced;
  r.queuePeak = s.queuePeak;
  r.lastError = s.lastError;
  r.stalls = _stallsThisBoot;
  r.longestGapMs = _longestGapMs;
  r.freeHeap = ESP.getFreeHeap();
  r.minFreeHeap = ESP.getMinFreeHeap();
}

// --- Printing -------------------------------------------------------------------------

static const char *profileName(int profile)
{
  switch (profile)
  {
  case SOAK_LOW:
    return "low";
  case SOAK_HIGH:
    return "high";
  default:
    return "bursts";
  }
}

static const char *resetName(uint8_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "power-on";
  case ESP_RST_SW:
    return "software";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT-WATCHDOG";
  case ESP_RST_TASK_WDT:
    return "TASK-WATCHDOG";
  case ESP_RST_WDT:
    return "WATCHDOG";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_USB:
    return "usb";
  default:
    return "other";
  }
}

static void printUptime(uint32_t seconds)
{
  Serial.printf("%lud%02luh%02lum%02lus", (unsigned long)(seconds / 86400),
                (unsigned long)(seconds / 3600 % 24), (unsigned long)(seconds / 60 % 60),
                (unsigned long)(seconds % 60));
}

static void printRecord(const char *label, const SoakRecord &r)
{
  Serial.printf("[%s] boot=%lu (%s) up=", label, (unsigned long)r.boot, resetName(r.resetReason));
  printUptime(r.uptimeS);
  Serial.printf(" profile=%s extraPeers=%u attempts=%lu accepted=%lu sent=%lu failed=%lu "
                "delivered=%lu notDelivered=%lu throttled=%lu skipped=%lu dropped=%lu replaced=%lu "
                "queuePeak=%lu restarts=%lu wifiRestarts=%lu "
                "lastError=%s stalls=%lu longestGap=%lums heap=%lu minHeap=%lu\n",
                profileName(r.profile), (unsigned)r.extraPeers, (unsigned long)r.attempts,
                (unsigned long)r.accepted, (unsigned long)r.sent, (unsigned long)r.failed,
                (unsigned long)r.delivered, (unsigned long)r.notDelivered,
                (unsigned long)r.throttled, (unsigned long)r.skipped, (unsigned long)r.dropped,
                (unsigned long)r.replaced, (unsigned long)r.queuePeak, (unsigned long)r.restarts,
                (unsigned long)r.wifiRestarts, esp_err_to_name((esp_err_t)r.lastError),
                (unsigned long)r.stalls, (unsigned long)r.longestGapMs,
                (unsigned long)r.freeHeap, (unsigned long)r.minFreeHeap);
}

static void printSaved()
{
  if (_firstStall.magic == kRecordMagic)
  {
    printRecord("first-stall", _firstStall);
  }
  else
  {
    Serial.println("[first-stall] none");
  }
  if (_checkpoint.magic == kRecordMagic)
  {
    printRecord("checkpoint", _checkpoint);
  }
}

static void printReport()
{
  static uint32_t lastAttempts = 0;
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  const uint32_t elapsed = now - lastMs;
  const uint32_t rate = elapsed ? (_attempts - lastAttempts) * 1000UL / elapsed : 0;
  lastAttempts = _attempts;
  lastMs = now;

  SoakRecord r;
  fillRecord(r);
  printRecord(_stalled ? "soak STALLED" : "soak", r);
  Serial.printf("[soak] rate=%lu/s savedStalls=%lu firstStall=", (unsigned long)rate,
                (unsigned long)_savedStalls);
  if (_firstStall.magic == kRecordMagic)
  {
    Serial.printf("boot %lu at ", (unsigned long)_firstStall.boot);
    printUptime(_firstStall.uptimeS);
    Serial.println();
  }
  else
  {
    Serial.println("none");
  }
}

static void printPeers()
{
  const int count = _client.espnowMIDI.getPeersCount();
  Serial.printf("[soak] peers: %d", count);
  for (int i = 0; i < count; i++)
  {
    uint8_t mac[6];
    if (_client.espnowMIDI.getPeer(i, mac))
    {
      Serial.printf(" %02X:%02X:%02X:%02X:%02X:%02X%s", mac[0], mac[1], mac[2], mac[3], mac[4],
                    mac[5], _client.espnowMIDI.isPeerReachable(mac) ? "" : " (unreachable)");
    }
  }
  Serial.println();
}

static void printHelp()
{
  Serial.println("[soak] commands: p = print now, c = clear saved results, "
                 "l = low, h = high, b = bursts, ? = help");
}

// --- Stall watch ----------------------------------------------------------------------

static void onAttempt()
{
  if (!_waitingForConfirm)
  {
    _waitingForConfirm = true;
    _firstUnconfirmedMs = millis();
  }
}

static void watchConfirmations()
{
  const uint32_t now = millis();
  const EspNowSendStats s = _client.getSendStats();
  const uint32_t confirms = s.delivered + s.notDelivered;

  if (confirms != _lastConfirms)
  {
    if (_waitingForConfirm)
    {
      const uint32_t gap = now - _firstUnconfirmedMs;
      if (gap > _longestGapMs)
      {
        _longestGapMs = gap;
      }
    }
    _lastConfirms = confirms;
    _waitingForConfirm = false;
    if (_stalled)
    {
      _stalled = false;
      Serial.printf("[event] recovered after %lums, restarts=%lu wifiRestarts=%lu\n",
                    (unsigned long)(now - _stallStartMs), (unsigned long)s.restarts,
                    (unsigned long)s.wifiRestarts);
    }
  }
  else if (_waitingForConfirm && !_stalled && now - _firstUnconfirmedMs >= SOAK_STALL_MS)
  {
    _stalled = true;
    _stallStartMs = _firstUnconfirmedMs;
    _stallsThisBoot++;
    _savedStalls++;
    saveStallCount();
    Serial.printf("[event] STALL: no confirmation for %lums, lastError=%s\n",
                  (unsigned long)(now - _firstUnconfirmedMs), esp_err_to_name(s.lastError));
    if (_firstStall.magic != kRecordMagic)
    {
      fillRecord(_firstStall);
      saveRecord("first", _firstStall);
      printRecord("first-stall", _firstStall);
    }
  }

  // The library retries every few seconds while stuck: log the first few only,
  // the [soak] line has the count
  static uint32_t restartsThisStall = 0;
  if (!_stalled)
  {
    restartsThisStall = 0;
  }
  if (s.restarts != _lastRestarts)
  {
    if (++restartsThisStall <= 3)
    {
      Serial.printf("[event] library restarted ESP-NOW (restarts=%lu wifiRestarts=%lu)\n",
                    (unsigned long)s.restarts, (unsigned long)s.wifiRestarts);
    }
    _lastRestarts = s.restarts;
  }
}

// --- Sending --------------------------------------------------------------------------

static bool sendMessage(uint32_t n)
{
  switch (n % 4)
  {
  case 0:
    return _client.sendNoteOn(60 + n % 12, 100, SOAK_CHANNEL);
  case 1:
    return _client.sendNoteOff(60 + n % 12, 0, SOAK_CHANNEL);
  case 2:
    return _client.sendControlChange(1, n % 128, SOAK_CHANNEL);
  default:
    return _client.sendPitchBend(static_cast<int>(n % 16384) - 8192, SOAK_CHANNEL);
  }
}

static void send()
{
  onAttempt();
  if (sendMessage(_attempts++))
  {
    _accepted++;
  }
}

// 7 messages 100 ms apart, then a 2 s pause
static void runLow(uint32_t now)
{
  static int step = 0;
  static uint32_t nextMs = 0;
  if ((int32_t)(now - nextMs) < 0)
  {
    return;
  }
  send();
  step = (step + 1) % 7;
  nextMs = now + (step == 0 ? 2000 : 100);
}

static void runHigh()
{
  static uint32_t nextUs = 0;
  const uint32_t now = micros();
  if ((int32_t)(now - nextUs) < 0)
  {
    return;
  }
  send();
  nextUs = now + 1000000UL / SOAK_HIGH_RATE;
}

static void runLoad(uint32_t now)
{
  switch (_profile)
  {
  case SOAK_LOW:
    runLow(now);
    break;
  case SOAK_HIGH:
    runHigh();
    break;
  default:
    if (now % SOAK_BURST_EVERY_MS < SOAK_BURST_S * 1000UL)
    {
      runHigh();
    }
    else
    {
      runLow(now);
    }
    break;
  }
}

// --- LED ------------------------------------------------------------------------------

static void updateLed(uint32_t now)
{
#ifdef LED_BUILTIN
  bool on;
  if (_stalled)
  {
    on = (now / 100) % 2 == 0;
  }
  else
  {
    const uint32_t t = now % 2000;
    on = t < 60 || (_savedStalls > 0 && t >= 200 && t < 260);
  }
  digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#else
  (void)now;
#endif
}

// --- Serial commands ------------------------------------------------------------------

static void handleCommands()
{
  while (Serial.available() > 0)
  {
    switch (Serial.read())
    {
    case 'p':
      printReport();
      printPeers();
      printSaved();
      break;
    case 'c':
      clearSaved();
      Serial.println("[soak] saved results cleared");
      break;
    case 'l':
      _profile = SOAK_LOW;
      Serial.println("[soak] profile low");
      break;
    case 'h':
      _profile = SOAK_HIGH;
      Serial.println("[soak] profile high");
      break;
    case 'b':
      _profile = SOAK_BURSTS;
      Serial.println("[soak] profile bursts");
      break;
    case '?':
      printHelp();
      break;
    default:
      break;
    }
  }
}

// --------------------------------------------------------------------------------------

static void onNoteOn(byte channel, byte note, byte velocity)
{
  if (SOAK_ECHO_CHANNEL != 0 && channel == SOAK_ECHO_CHANNEL && velocity == 99)
  {
    _client.sendNoteOn(note, velocity, channel);
  }
}

void setup()
{
  Serial.begin(115200);
  _resetReason = esp_reset_reason();
  loadSaved();
  // Give a serial monitor a moment to attach, so the saved results are seen
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 3000;)
  {
    delay(10);
  }
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#endif

  Serial.printf("[soak] boot %lu, reset reason %s, profile %s, extra peers %d, inputs %s\n",
                (unsigned long)_boot, resetName(_resetReason), profileName(_profile),
                SOAK_EXTRA_PEERS, SOAK_KEEP_INPUTS ? "on" : "idle");
  printSaved();
  printHelp();

#if !SOAK_KEEP_INPUTS
  _client.io.setAnalogReader([](uint8_t) { return 0; });
  _client.io.setDigitalReader([](uint8_t) { return HIGH; });
#endif
  _client.setHandleNoteOn(onNoteOn);
  _client.begin();
  _client.addPeer(dongleMac); // stored; fails harmlessly when it is already
  for (int i = 0; i < SOAK_EXTRA_PEERS && i < 3; i++)
  {
    _client.espnowMIDI.addPeer(kExtraPeers[i]); // not stored
  }
  printPeers();
}

void loop()
{
  _client.loop();
  handleCommands();

  const uint32_t now = millis();
  runLoad(now);
  watchConfirmations();
  updateLed(now);

  static uint32_t lastReportMs = 0;
  if (now - lastReportMs >= SOAK_REPORT_MS)
  {
    lastReportMs = now;
    printReport();
  }

  static uint32_t lastCheckpointMs = 0;
  if (now - lastCheckpointMs >= SOAK_CHECKPOINT_MS)
  {
    lastCheckpointMs = now;
    fillRecord(_checkpoint);
    saveRecord("check", _checkpoint);
  }
}
