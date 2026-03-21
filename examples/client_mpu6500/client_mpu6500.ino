/*
  Read an MPU6500 via the MPU9250_WE library (MPU6500_WE) and send MIDI control change
  messages over ESP-NOW.

  Dependency: https://github.com/wollewald/MPU9250_WE (Arduino Library Manager: "MPU9250_WE")

  Dongle: run examples/print_mac and set peerMacAddress below (or use addPeerFromString).
*/

#include "config.h"
#include "enomik_client.h"

#include <MPU6500_WE.h>
#include <Wire.h>

#define MPU6500_ADDR 0x68

// MPU9250_WE package — MPU6500_WE class, see library examples (e.g. MPU6500_all_data.ino)
MPU6500_WE myMPU6500 = MPU6500_WE(MPU6500_ADDR);

enomik::Client _client;

// on the dongle: run the print_mac firmware and paste it here
uint8_t peerMacAddress[6] = {0xCC, 0x8D, 0xA2, 0x8B, 0x85, 0x1C};

bool _mpuOk = false;
unsigned long _lastSendMs = 0;

static uint8_t mapAccelG(float g) {
  // ±2 g default range — map to 0…127
  return (uint8_t)constrain(map((long)(g * 1000), -2000, 2000, 0, 127), 0, 127);
}

static uint8_t mapAccelSumG(float sumG) {
  // x + y + z: each axis ±2 g → sum roughly in [-6, 6] g
  return (uint8_t)constrain(map((long)(sumG * 1000), -6000, 6000, 0, 127), 0, 127);
}

static uint8_t mapGyroDps(float dps) {
  // ±250 °/s default — map to 0…127
  return (uint8_t)constrain(map((long)(dps * 100), -25000, 25000, 0, 127), 0, 127);
}

static uint8_t mapTempC(float tempC) {
  // Rough room / device range for MIDI
  return (uint8_t)constrain(map((long)(tempC * 10), -200, 850, 0, 127), 0, 127);
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  _client.begin();

  _mpuOk = myMPU6500.init();
  if (!_mpuOk) {
    Serial.println(F("MPU6500 does not respond"));
    return;
  }
  Serial.println(F("MPU6500 is connected"));

  Serial.println(F("Place MPU6500 flat and still — calibrating offsets..."));
  delay(1000);
  myMPU6500.autoOffsets();
  Serial.println(F("Calibration done."));

  myMPU6500.enableGyrDLPF();
  myMPU6500.setGyrDLPF(MPU6500_DLPF_6);
  myMPU6500.setSampleRateDivider(5);
  myMPU6500.setGyrRange(MPU6500_GYRO_RANGE_250);
  myMPU6500.setAccRange(MPU6500_ACC_RANGE_2G);
  myMPU6500.enableAccDLPF(true);
  myMPU6500.setAccDLPF(MPU6500_DLPF_6);

  delay(200);

  _client.sendControlChange(127, 127, 16);
}

void loop() {
  _client.loop();

  if (!_mpuOk) {
    return;
  }

  unsigned long now = millis();
  if (now - _lastSendMs < MIDI_SEND_INTERVAL_MS) {
    return;
  }
  _lastSendMs = now;

#if MPU6500_NEED_ACCEL
  xyzFloat gValue = myMPU6500.getGValues();
#endif
#if MPU6500_ANY_GYRO
  xyzFloat gyr = myMPU6500.getGyrValues();
#endif
#if ENABLE_MPU6500_TEMP
  float temp = myMPU6500.getTemperature();
#endif

#if ENABLE_MPU6500_ACCEL_X
  _client.sendControlChange(CC_MPU6500_ACCEL_X, mapAccelG(gValue.x), MIDI_CHANNEL);
#endif
#if ENABLE_MPU6500_ACCEL_Y
  _client.sendControlChange(CC_MPU6500_ACCEL_Y, mapAccelG(gValue.y), MIDI_CHANNEL);
#endif
#if ENABLE_MPU6500_ACCEL_Z
  _client.sendControlChange(CC_MPU6500_ACCEL_Z, mapAccelG(gValue.z), MIDI_CHANNEL);
#endif
#if ENABLE_MPU6500_ACCEL_SUM
  {
    float accelSum = gValue.x + gValue.y + gValue.z;
    _client.sendControlChange(CC_MPU6500_ACCEL_SUM, mapAccelSumG(accelSum), MIDI_CHANNEL);
  }
#endif
#if ENABLE_MPU6500_GYRO_X
  _client.sendControlChange(CC_MPU6500_GYRO_X, mapGyroDps(gyr.x), MIDI_CHANNEL);
#endif
#if ENABLE_MPU6500_GYRO_Y
  _client.sendControlChange(CC_MPU6500_GYRO_Y, mapGyroDps(gyr.y), MIDI_CHANNEL);
#endif
#if ENABLE_MPU6500_GYRO_Z
  _client.sendControlChange(CC_MPU6500_GYRO_Z, mapGyroDps(gyr.z), MIDI_CHANNEL);
#endif
#if ENABLE_MPU6500_TEMP
  _client.sendControlChange(CC_MPU6500_TEMP, mapTempC(temp), MIDI_CHANNEL);
#endif
}
