#include "enomik_client.h"

// on the dongle: run the print_mac firmware and paste it here
// uint8_t peerMacAddress[6] = { 0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C };
enomik::Client _client;

void setup() {
  Serial.begin(115200);
  _client.setName("client"); // optional
  _client.begin();
  // _client.addPeer(peerMacAddress);
}

void loop() {
  _client.loop();
}