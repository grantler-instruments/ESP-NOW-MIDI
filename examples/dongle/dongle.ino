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

  _dongle.begin();
}

void loop() {
  _dongle.loop();
}
