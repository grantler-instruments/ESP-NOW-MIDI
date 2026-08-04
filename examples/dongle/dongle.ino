#include "./config.h"
#include "enomik_dongle.h"

#if HAS_DISPLAY == 1
#include "SSD1306Display.h"
#endif

enomik::Dongle _dongle;

#if HAS_DISPLAY == 1
SSD1306Display display;
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

#if HAS_DISPLAY == 1
  _dongle.setDisplay(&display);
#endif

  // Optional: customize USB identity before begin()
  // _dongle.setManufacturerDescriptor("grantler instruments");
  // _dongle.setProductDescriptor("enomik3000_dongle");

  // Optional: hard-add a known peer (or wait for auto-discovery)
  // uint8_t clientMac[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
  // _dongle.addPeer(clientMac);

  // --- Bridge filters (optional) -----------------------------------------
  // midi_message fields: status, channel (1-16), firstByte, secondByte
  // Return true to forward (after any edits), false to drop.
  // Keep filters non-blocking. Does not apply to send*() inject APIs.
  //
  // Drop channel 10 (GM drums) before it reaches the computer:
  // _dongle.setToHostFilter([](midi_message& msg) {
  //   return msg.channel != 10;
  // });
  //
  // Force everything from the computer onto MIDI channel 1:
  // _dongle.setFromHostFilter([](midi_message& msg) {
  //   msg.channel = 1;
  //   return true;
  // });
  //
  // Drop sustain pedal (CC 64) from the computer; pass everything else:
  // _dongle.setFromHostFilter([](midi_message& msg) {
  //   if (msg.status == MIDI_CONTROL_CHANGE && msg.firstByte == 64) {
  //     return false;
  //   }
  //   return true;
  // });
  //
  // Clear a filter later:
  // _dongle.setToHostFilter(nullptr);
  // _dongle.setFromHostFilter(nullptr);

  _dongle.begin();
}

void loop() {
  _dongle.loop();
}
