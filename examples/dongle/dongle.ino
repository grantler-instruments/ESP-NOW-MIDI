#include "./config.h"
#include <WiFi.h>
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <esp_now_midi.h>
#include <esp_system.h>
#include "MidiMessageHistory.h"
#include "UsbMidiQueue.h"

#if HAS_DISPLAY == 1
#include "SSD1306Display.h"
#endif

String version = getVersion();

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

esp_now_midi* espnowMIDI = nullptr;
#if HAS_DISPLAY == 1
static Display* display = nullptr;
static uint32_t lastDisplayUpdate = 0;
#endif

uint8_t baseMac[6];
String macStr;



MidiMessageHistory messageHistory[MAX_HISTORY];
int messageIndex = 0;
static UsbMidiQueue usbMidiQueue;

// Function to add a new message to the history
void addToHistory(const midi_message& msg, bool outgoing = false) {
  messageHistory[messageIndex].message = msg;
  messageHistory[messageIndex].outgoing = outgoing;
  messageHistory[messageIndex].timestamp = millis();
  messageIndex = (messageIndex + 1) % MAX_HISTORY;
}

void queueFromEspNow(const midi_message& msg, bool addHistory = true) {
  if (addHistory) {
    addToHistory(msg, false);
  }
  usbMidiQueue.enqueue(msg);
}

bool sendQueuedMidi(const midi_message& msg) {
  const uint8_t ch = (msg.channel - 1) & 0x0F;
  uint8_t packet[4] = {0, 0, 0, 0};

  switch (msg.status) {
    case MIDI_NOTE_ON:
      packet[0] = 0x09;
      packet[1] = MIDI_NOTE_ON | ch;
      packet[2] = msg.firstByte;
      packet[3] = msg.secondByte;
      break;
    case MIDI_NOTE_OFF:
      packet[0] = 0x08;
      packet[1] = MIDI_NOTE_OFF | ch;
      packet[2] = msg.firstByte;
      packet[3] = msg.secondByte;
      break;
    case MIDI_CONTROL_CHANGE:
      packet[0] = 0x0B;
      packet[1] = MIDI_CONTROL_CHANGE | ch;
      packet[2] = msg.firstByte;
      packet[3] = msg.secondByte;
      break;
    case MIDI_PROGRAM_CHANGE:
      packet[0] = 0x0C;
      packet[1] = MIDI_PROGRAM_CHANGE | ch;
      packet[2] = msg.firstByte;
      break;
    case MIDI_AFTERTOUCH:
      packet[0] = 0x0D;
      packet[1] = MIDI_AFTERTOUCH | ch;
      packet[2] = msg.firstByte;
      break;
    case MIDI_POLY_AFTERTOUCH:
      packet[0] = 0x0A;
      packet[1] = MIDI_POLY_AFTERTOUCH | ch;
      packet[2] = msg.firstByte;
      packet[3] = msg.secondByte;
      break;
    case MIDI_PITCH_BEND:
      packet[0] = 0x0E;
      packet[1] = MIDI_PITCH_BEND | ch;
      packet[2] = msg.firstByte;
      packet[3] = msg.secondByte;
      break;
    case MIDI_START:
      packet[0] = 0x0F;
      packet[1] = MIDI_START;
      break;
    case MIDI_STOP:
      packet[0] = 0x0F;
      packet[1] = MIDI_STOP;
      break;
    case MIDI_CONTINUE:
      packet[0] = 0x0F;
      packet[1] = MIDI_CONTINUE;
      break;
    case MIDI_TIME_CLOCK:
      packet[0] = 0x0F;
      packet[1] = MIDI_TIME_CLOCK;
      break;
    case MIDI_SONG_POS_POINTER:
      packet[0] = 0x03;
      packet[1] = MIDI_SONG_POS_POINTER;
      packet[2] = msg.firstByte;
      packet[3] = msg.secondByte;
      break;
    case MIDI_SONG_SELECT:
      packet[0] = 0x02;
      packet[1] = MIDI_SONG_SELECT;
      packet[2] = msg.firstByte;
      break;
    default:
      return true;
  }

  return usb_midi.writePacket(packet);
}

void drainUsbMidiQueue() {
  if (!TinyUSBDevice.mounted()) {
    return;
  }

  if (TinyUSBDevice.suspended()) {
    if (usbMidiQueue.hasPending()) {
      TinyUSBDevice.remoteWakeup();
    }
    return;
  }

  if (!TinyUSBDevice.ready()) {
    return;
  }

  midi_message msg;
  while (usbMidiQueue.peek(msg)) {
    if (!sendQueuedMidi(msg)) {
      break;
    }
    usbMidiQueue.consumeHead();
  }
}

char getUsbStatusChar() {
  if (!TinyUSBDevice.mounted()) {
    return 'D';
  }
  if (TinyUSBDevice.suspended()) {
    return 'S';
  }
  if (usbMidiQueue.hasPending()) {
    return 'Q';
  }
  return 'C';  // Connected / ready
}

void logUsbState(unsigned long now) {
  static char lastStatus = 0;
  static uint32_t lastLogMs = 0;
  const char status = getUsbStatusChar();

  if (status == lastStatus && (status == 'C' || (now - lastLogMs) < 10000)) {
    return;
  }

  lastStatus = status;
  lastLogMs = now;
  Serial.printf("USB status=%c mounted=%d suspended=%d ready=%d queue=%u\n",
                status,
                TinyUSBDevice.mounted(),
                TinyUSBDevice.suspended(),
                TinyUSBDevice.ready(),
                usbMidiQueue.pendingCount());
}

void readMacAddress() {
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  if (ret == ESP_OK) {
    Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
    macStr = String(baseMac[0], HEX) + ":" + String(baseMac[1], HEX) + ":" + String(baseMac[2], HEX) + ":" + String(baseMac[3], HEX) + ":" + String(baseMac[4], HEX) + ":" + String(baseMac[5], HEX);
  } else {
    Serial.println("Failed to read MAC address");
  }
}

#if HAS_DISPLAY == 1
void updateDisplay();
#endif

// ESP-NOW MIDI receive handlers - queue for USB MIDI (sent from loop())
void handleNoteOn(byte channel, byte note, byte velocity) {
  midi_message msg;
  msg.status = MIDI_NOTE_ON;
  msg.channel = channel;
  msg.firstByte = note;
  msg.secondByte = velocity;
  queueFromEspNow(msg);
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  midi_message msg;
  msg.status = MIDI_NOTE_OFF;
  msg.channel = channel;
  msg.firstByte = note;
  msg.secondByte = velocity;
  queueFromEspNow(msg);
}

void handleControlChange(byte channel, byte control, byte value) {
  midi_message msg;
  msg.status = MIDI_CONTROL_CHANGE;
  msg.channel = channel;
  msg.firstByte = control;
  msg.secondByte = value;
  queueFromEspNow(msg);
}

void handleProgramChange(byte channel, byte program) {
  midi_message msg;
  msg.status = MIDI_PROGRAM_CHANGE;
  msg.channel = channel;
  msg.firstByte = program;
  msg.secondByte = 0;
  queueFromEspNow(msg);
}

void handleAfterTouchChannel(byte channel, byte pressure) {
  midi_message msg;
  msg.status = MIDI_AFTERTOUCH;
  msg.channel = channel;
  msg.firstByte = pressure;
  msg.secondByte = 0;
  queueFromEspNow(msg);
}

void handleAfterTouchPoly(byte channel, byte note, byte pressure) {
  midi_message msg;
  msg.status = MIDI_POLY_AFTERTOUCH;
  msg.channel = channel;
  msg.firstByte = note;
  msg.secondByte = pressure;
  queueFromEspNow(msg);
}

void handlePitchBend(byte channel, int value) {
  midi_message msg;
  msg.status = MIDI_PITCH_BEND;
  msg.channel = channel;
  const int unsignedValue = value + 8192;
  msg.firstByte = unsignedValue & 0x7F;
  msg.secondByte = (unsignedValue >> 7) & 0x7F;
  queueFromEspNow(msg);
}

void handleStart() {
  midi_message msg;
  msg.status = MIDI_START;
  msg.channel = 0;
  msg.firstByte = 0;
  msg.secondByte = 0;
  queueFromEspNow(msg);
}

void handleStop() {
  midi_message msg;
  msg.status = MIDI_STOP;
  msg.channel = 0;
  msg.firstByte = 0;
  msg.secondByte = 0;
  queueFromEspNow(msg);
}

void handleContinue() {
  midi_message msg;
  msg.status = MIDI_CONTINUE;
  msg.channel = 0;
  msg.firstByte = 0;
  msg.secondByte = 0;
  queueFromEspNow(msg);
}

void handleClock() {
  usbMidiQueue.enqueueClock();
}

void handleSongPosition(uint16_t value) {
  midi_message msg;
  msg.status = MIDI_SONG_POS_POINTER;
  msg.channel = 0;
  msg.firstByte = value & 0x7F;
  msg.secondByte = (value >> 7) & 0x7F;
  queueFromEspNow(msg, false);
}

void handleSongSelect(byte value) {
  midi_message msg;
  msg.status = MIDI_SONG_SELECT;
  msg.channel = 0;
  msg.firstByte = value;
  msg.secondByte = 0;
  queueFromEspNow(msg);
}

// USB MIDI receive handlers - forward to ESP-NOW
void onNoteOn(byte channel, byte pitch, byte velocity) {
  midi_message msg;
  msg.status = MIDI_NOTE_ON;
  msg.channel = channel;
  msg.firstByte = pitch;
  msg.secondByte = velocity;
  addToHistory(msg, true);

  espnowMIDI->sendNoteOn(pitch, velocity, channel);
}

void onNoteOff(byte channel, byte pitch, byte velocity) {
  midi_message msg;
  msg.status = MIDI_NOTE_OFF;
  msg.channel = channel;
  msg.firstByte = pitch;
  msg.secondByte = velocity;
  addToHistory(msg, true);

  espnowMIDI->sendNoteOff(pitch, velocity, channel);
}

void onControlChange(byte channel, byte controller, byte value) {
  midi_message msg;
  msg.status = MIDI_CONTROL_CHANGE;
  msg.channel = channel;
  msg.firstByte = controller;
  msg.secondByte = value;
  addToHistory(msg, true);

  espnowMIDI->sendControlChange(controller, value, channel);
}

void onProgramChange(byte channel, byte program) {
  midi_message msg;
  msg.status = MIDI_PROGRAM_CHANGE;
  msg.channel = channel;
  msg.firstByte = program;
  msg.secondByte = 0;
  addToHistory(msg, true);

  espnowMIDI->sendProgramChange(program, channel);
}

void onAfterTouch(byte channel, byte pressure) {
  midi_message msg;
  msg.status = MIDI_AFTERTOUCH;
  msg.channel = channel;
  msg.firstByte = pressure;
  msg.secondByte = 0;
  addToHistory(msg, true);

  espnowMIDI->sendAfterTouch(pressure, channel);
}

void onPolyAfterTouch(byte channel, byte note, byte pressure) {
  midi_message msg;
  msg.status = MIDI_POLY_AFTERTOUCH;
  msg.channel = channel;
  msg.firstByte = note;
  msg.secondByte = pressure;
  addToHistory(msg, true);

  espnowMIDI->sendAfterTouchPoly(note, pressure, channel);
}

void onPitchBend(byte channel, int value) {
  midi_message msg;
  msg.status = MIDI_PITCH_BEND;
  msg.channel = channel;
  msg.firstByte = value & 0x7F;
  msg.secondByte = (value >> 7) & 0x7F;
  addToHistory(msg, true);

  // Convert from MIDI library format (0-16383) to signed (-8192 to 8191)
  espnowMIDI->sendPitchBend(value - 8192, channel);
}

void onStart() {
  midi_message msg;
  msg.status = MIDI_START;
  msg.channel = 0;
  msg.firstByte = 0;
  msg.secondByte = 0;
  addToHistory(msg, true);

  espnowMIDI->sendStart();
}

void onStop() {
  midi_message msg;
  msg.status = MIDI_STOP;
  msg.channel = 0;
  msg.firstByte = 0;
  msg.secondByte = 0;
  addToHistory(msg, true);

  espnowMIDI->sendStop();
}

void onContinue() {
  midi_message msg;
  msg.status = MIDI_CONTINUE;
  msg.channel = 0;
  msg.firstByte = 0;
  msg.secondByte = 0;
  addToHistory(msg, true);

  espnowMIDI->sendContinue();
}

void onClock() {
  // Don't print or add to history - too many messages
  espnowMIDI->sendClock();
}

void onSongPosition(unsigned int value) {
  // Don't add to history - too frequent
  espnowMIDI->sendSongPosition(value);
}

void onSongSelect(byte value) {
  midi_message msg;
  msg.status = MIDI_SONG_SELECT;
  msg.channel = 0;
  msg.firstByte = value;
  msg.secondByte = 0;
  addToHistory(msg, true);

  espnowMIDI->sendSongSelect(value);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP-NOW MIDI DONGLE ===");
  Serial.printf("ESP-IDF Version: %s\n", esp_get_idf_version());
  Serial.printf("Channel: %d\n", ESP_NOW_MIDI_CHANNEL);


  // Initialize USB MIDI
  TinyUSBDevice.setManufacturerDescriptor("grantler instruments");
  TinyUSBDevice.setProductDescriptor("enomik3000_dongle");

  usb_midi.begin();


  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(100);
  }

  // TinyUSBDevice.setVbusDetection(false);
  TinyUSBDevice.attach();

  // Initialize ESP-NOW MIDI library
  espnowMIDI = new esp_now_midi();
  espnowMIDI->begin();

  readMacAddress();
  Serial.print("Mac: ");
  Serial.println(macStr);

  // Set ESP-NOW receive handlers
  espnowMIDI->setHandleNoteOn(handleNoteOn);
  espnowMIDI->setHandleNoteOff(handleNoteOff);
  espnowMIDI->setHandleControlChange(handleControlChange);
  espnowMIDI->setHandleProgramChange(handleProgramChange);
  espnowMIDI->setHandlePitchBend(handlePitchBend);
  espnowMIDI->setHandleAfterTouchChannel(handleAfterTouchChannel);
  espnowMIDI->setHandleAfterTouchPoly(handleAfterTouchPoly);
  espnowMIDI->setHandleStart(handleStart);
  espnowMIDI->setHandleStop(handleStop);
  espnowMIDI->setHandleContinue(handleContinue);
  espnowMIDI->setHandleClock(handleClock);
  espnowMIDI->setHandleSongPosition(handleSongPosition);
  espnowMIDI->setHandleSongSelect(handleSongSelect);

  // Add known peer (optional - or wait for auto-discovery)
  // uint8_t clientMac[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
  // espnowMIDI->addPeer(clientMac);

  Serial.print("Registered peers: ");
  Serial.println(espnowMIDI->getPeersCount());



  // MIDI.begin(MIDI_CHANNEL_OMNI);

  // // Set USB MIDI transmit handlers
  // MIDI.setHandleNoteOn(onNoteOn);
  // MIDI.setHandleNoteOff(onNoteOff);
  // MIDI.setHandleControlChange(onControlChange);
  // MIDI.setHandleProgramChange(onProgramChange);
  // MIDI.setHandlePitchBend(onPitchBend);
  // MIDI.setHandleAfterTouchChannel(onAfterTouch);
  // MIDI.setHandleAfterTouchPoly(onPolyAfterTouch);
  // MIDI.setHandleStart(onStart);
  // MIDI.setHandleStop(onStop);
  // MIDI.setHandleContinue(onContinue);
  // MIDI.setHandleClock(onClock);
  // MIDI.setHandleSongPosition(onSongPosition);
  // MIDI.setHandleSongSelect(onSongSelect);

  // Initialize display
#if HAS_DISPLAY == 1
  static SSD1306Display ssd1306;

  display = &ssd1306;

  if (!display->begin()) {
    Serial.println("Display init failed");
    display = nullptr;
  }
#endif

  Serial.println("Setup complete - ready!");
}

void loop() {
  unsigned long now = millis();
  static bool usbMidiInitialized = false;

  if (usbMidiInitialized && !TinyUSBDevice.mounted()) {
    Serial.println("USB disconnected");
    usbMidiInitialized = false;
    usbMidiQueue.clear();
  }

  // Wait for USB to mount, then initialize MIDI
  if (!usbMidiInitialized && TinyUSBDevice.mounted()) {
    Serial.println("USB mounted - initializing MIDI");

    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI.turnThruOff();

    // Set USB MIDI handlers
    MIDI.setHandleNoteOn(onNoteOn);
    MIDI.setHandleNoteOff(onNoteOff);
    MIDI.setHandleControlChange(onControlChange);
    MIDI.setHandleProgramChange(onProgramChange);
    MIDI.setHandlePitchBend(onPitchBend);
    MIDI.setHandleAfterTouchChannel(onAfterTouch);
    MIDI.setHandleAfterTouchPoly(onPolyAfterTouch);
    MIDI.setHandleStart(onStart);
    MIDI.setHandleStop(onStop);
    MIDI.setHandleContinue(onContinue);
    MIDI.setHandleClock(onClock);
    MIDI.setHandleSongPosition(onSongPosition);
    MIDI.setHandleSongSelect(onSongSelect);

    usbMidiInitialized = true;
    Serial.println("USB MIDI ready!");
  }

  if (usbMidiInitialized) {
    MIDI.read();
    drainUsbMidiQueue();
  }

  logUsbState(now);

#if HAS_DISPLAY == 1
  if (display && (now - lastDisplayUpdate) >= UPDATE_DISPLAY_INTERVAL) {
    lastDisplayUpdate = now;

    display->update(
      baseMac,
      version.c_str(),
      espnowMIDI->getPeersCount(),
      getUsbStatusChar(),
      messageHistory,
      MAX_HISTORY,
      messageIndex);
  }
#endif
}
