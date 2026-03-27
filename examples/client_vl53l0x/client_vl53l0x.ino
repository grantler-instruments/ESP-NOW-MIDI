/*
  Read a VL53L0X time-of-flight sensor via the Adafruit library and send MIDI control change
  messages over ESP-NOW.

  Dependency: Arduino Library Manager — "Adafruit VL53L0X" (also installs Adafruit BusIO).

  Dongle: run examples/print_mac and set peerMacAddress below (or use addPeerFromString).
*/

#include "config.h"
#include "enomik_client.h"

#include <Adafruit_VL53L0X.h>
#include <Wire.h>

Adafruit_VL53L0X lox;

enomik::Client _client;

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = {0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C};

bool _vl53Ok = false;
unsigned long _lastSendMs = 0;

static uint8_t mapDistanceMm(uint16_t mm) {
  return (uint8_t)constrain(
      map((long)mm, VL53L0X_MAP_MM_MIN, VL53L0X_MAP_MM_MAX, 0, 127), 0, 127);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin();

  _client.begin();

  _vl53Ok = lox.begin();
  if (!_vl53Ok) {
    Serial.println(F("VL53L0X does not respond"));
    return;
  }
  Serial.println(F("VL53L0X is connected"));

  delay(200);

  _client.sendControlChange(127, 127, 16);
}

void loop() {
  _client.loop();

  if (!_vl53Ok) {
    Serial.println("not ok");
    return;
  }

  unsigned long now = millis();
  if (now - _lastSendMs < MIDI_SEND_INTERVAL_MS) {
    return;
  }
  _lastSendMs = now;

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  // RangeStatus 4 = phase failure / unreliable (see ST VL53L0X API notes)
  uint8_t ccValue = 0;
  if (measure.RangeStatus != 4) {
    ccValue = mapDistanceMm(measure.RangeMilliMeter);
  }

  _client.sendControlChange(CC_VL53L0X_DISTANCE, ccValue, MIDI_CHANNEL);
}
