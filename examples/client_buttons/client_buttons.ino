#include "config.h"
#include "enomik_client.h"
#include <AceButton.h>
using namespace ace_button;

enomik::Client _client;

AceButton _buttons[NUMBER_OF_BUTTONS];

void handleEvent(AceButton *, uint8_t, uint8_t);

void setup() {
  Serial.begin(115200);
  delay(1000);
  _client.setName("client_buttons"); // optional
  _client.begin();
  uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };
  _client.addPeer(peerMacAddress);
  // _client.addPeerFromString("84:F7:03:F2:54:62");

  for (auto i = 0; i < NUMBER_OF_BUTTONS; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    _buttons[i].init(buttonPins[i], HIGH, i);
  }

  ButtonConfig *buttonConfig = ButtonConfig::getSystemButtonConfig();
  buttonConfig->setEventHandler(handleEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
  buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
}

void loop() {
  _client.loop();
  for (auto i = 0; i < NUMBER_OF_BUTTONS; i++) {
    _buttons[i].check();
  }
}

void handleEvent(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  Serial.print(F("handleEvent(): eventType: "));
  Serial.print(AceButton::eventName(eventType));
  Serial.print(F("; buttonState: "));
  Serial.println(buttonState);

  uint8_t id = button->getId();
  switch (eventType) {
    case AceButton::kEventPressed:
      _client.sendNoteOn(MIDI_NOTE + id, 127, 1);
      break;
    case AceButton::kEventReleased:
      _client.sendNoteOff(MIDI_NOTE + id, 0, 1);
      break;
  }
}