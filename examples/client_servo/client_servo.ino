/*
  Servo client: MIDI control change (ESP-NOW) -> hobby servo angle.

  Based on the Arduino "Sweep" tutorial (BARRAGAN / Scott Fitzgerald / John Bennett,
  public domain) and ESP32Servo usage patterns — see
  https://www.arduino.cc/en/Tutorial/Sweep

  Dependency: ESP32Servo (Library Manager or https://github.com/madhephaestus/ESP32Servo)

  Typical hobby servos expect ~50 Hz with pulse widths roughly 500–2500 µs for 0–180°;
  adjust attach() min/max if your servo stops short or binds.

  Power: servos draw significant current — use VBat or an external 5 V supply with
  common ground; do not rely on the 3.3 V logic rail for motor power.

  Dongle: run examples/print_mac and set peerMacAddress below (or rely on stored peers).

  CC scaling (SERVO_CC_VALUE_IS_DEGREES):
  - 1 (default): CC value is the angle in degrees (e.g. value 90 → 90°). MIDI is 7-bit,
    so you can only command 0–127° unless you use scaling below.
  - 0: map CC 0–127 across 0–180° (then value 90 → ~127°, not 90°).

  Incoming CC is handled inside the ESP-NOW receive path; the servo is updated from
  loop() so PWM writes stay off the WiFi callback stack.
*/

#include "enomik_client.h"
#include <ESP32Servo.h>

// GPIO for servo signal (PWM-capable pin for your board)
#define SERVO_PIN 21 // i ran into issues with the default pin 18, needs to be double checked, but pin 21 works fine

// MIDI routing — match your DAW / dongle
#define SERVO_CC SERVO_PIN
#define MIDI_CHANNEL 1

// 1 = CC value == degrees (90 → 90°). 0 = map 0–127 → 0–180°
#define SERVO_CC_VALUE_IS_DEGREES 0

// on the dongle: run the print_mac firmware and paste it here
// uint8_t peerMacAddress[6] = { 0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62 };

enomik::Client _client;
Servo _servo;
bool _isDirty = false;

static portMUX_TYPE _servoMux = portMUX_INITIALIZER_UNLOCKED;
static volatile int _servoAnglePending = 90;

void onControlChange(byte channel, byte control, byte value) {
  Serial.println(channel);
  Serial.println(control);
  if (channel != MIDI_CHANNEL) {
    return;
  }
  if (control != SERVO_CC) {
    return;
  }
  int angle;
#if SERVO_CC_VALUE_IS_DEGREES
  angle = constrain((int)value, 0, 180);
#else
  angle = (int)map(value, 0, 127, 0, 180);
  angle = constrain(angle, 0, 180);
#endif

  _servoAnglePending = angle;
  _isDirty = true;
}

void setup() {
  Serial.begin(115200);
  ESP32PWM::allocateTimer(3);
  _servo.setPeriodHertz(50);
  _servo.attach(SERVO_PIN, 500, 2400);
  _servo.write(90);

  _client.begin();
  // _client.addPeer(peerMacAddress);
  _client.setHandleControlChange(onControlChange);
}

void loop() {
  _client.loop();

  if (_isDirty) {
    _servo.write(_servoAnglePending);
    Serial.println(_servoAnglePending);
    _isDirty = false;
  }
}
