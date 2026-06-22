/*
  NeoPixel client: MIDI note-on (ESP-NOW) -> WS2812 RGB strip.

  Hardware: 24 WS2812/NeoPixel RGB LEDs on GPIO 2.

  Dependency: Adafruit NeoPixel (Library Manager or
  https://github.com/adafruit/Adafruit_NeoPixel)

  Mapping (note on):
    - pitch    -> LED index (0..NUM_LEDS-1)
    - velocity -> color channel value (0..127 scaled to 0..255)
    - MIDI channel selects the color component:
        ch 1 -> red, ch 2 -> green, ch 3 -> blue
  Each color component is tracked independently per LED, so a note on one
  channel does not disturb the components set by the others. Note off clears
  only that channel's component for the LED.

  The strip is only refreshed from loop() every UPDATE_INTERVAL_MS, and only
  when something actually changed, to keep show() off the WiFi receive callback
  and avoid redundant refreshes.

  Power: 24 LEDs at full brightness can draw ~1.4 A; use an external 5 V supply
  with common ground rather than the 3.3 V logic rail.

  Dongle: run examples/print_mac and set peerMacAddress below (or rely on stored peers).
*/

#include "enomik_client.h"
#include <Adafruit_NeoPixel.h>

#define LED_PIN 2
#define NUM_LEDS 24

// MIDI channels mapped to color components
#define RED_CHANNEL 1
#define GREEN_CHANNEL 2
#define BLUE_CHANNEL 3

// minimum time between strip refreshes
#define UPDATE_INTERVAL_MS 16

// on the dongle: run the print_mac firmware and paste it here
// uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };

enomik::Client _client;
Adafruit_NeoPixel _strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// per-LED color components, set independently by the three MIDI channels
uint8_t _rgb[NUM_LEDS][3] = { 0 };

volatile bool _isDirty = false;
unsigned long _lastUpdate = 0;

// returns the rgb component index (0=red, 1=green, 2=blue) for a MIDI channel,
// or -1 if the channel is not mapped to a color
int componentForChannel(byte channel) {
  switch (channel) {
    case RED_CHANNEL: return 0;
    case GREEN_CHANNEL: return 1;
    case BLUE_CHANNEL: return 2;
    default: return -1;
  }
}

void setComponent(byte note, int component, uint8_t value) {
  if (note >= NUM_LEDS || component < 0) {
    return;
  }
  _rgb[note][component] = value;
  _strip.setPixelColor(note, _strip.Color(_rgb[note][0], _rgb[note][1], _rgb[note][2]));
  _isDirty = true;
}

void onNoteOn(byte channel, byte note, byte velocity) {
  int component = componentForChannel(channel);
  if (component < 0) {
    return;
  }
  // velocity 0 is treated as note off per MIDI convention
  uint8_t value = (velocity == 0) ? 0 : map(velocity, 0, 127, 0, 255);
  setComponent(note, component, value);
}

void onNoteOff(byte channel, byte note, byte velocity) {
  int component = componentForChannel(channel);
  if (component < 0) {
    return;
  }
  setComponent(note, component, 0);
}

void setup() {
  Serial.begin(115200);

  _strip.begin();
  _strip.clear();
  _strip.show();

  _client.begin();
  // _client.addPeer(peerMacAddress);
  _client.setHandleNoteOn(onNoteOn);
  _client.setHandleNoteOff(onNoteOff);
}

void loop() {
  _client.loop();

  unsigned long now = millis();
  if (_isDirty && (now - _lastUpdate >= UPDATE_INTERVAL_MS)) {
    _isDirty = false;
    _lastUpdate = now;
    _strip.show();
  }
}
