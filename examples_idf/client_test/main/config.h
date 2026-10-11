#pragma once

// USB MIDI (wizard Phase 1) comes from utils/esp.h on ESP32-S2/S3
// (HAS_USB_MIDI). Build with idf.py set-target esp32s2 or esp32s3.

// Channel the test client echoes back on. Incoming channel-voice messages on
// this channel are mirrored straight back out (USB + ESP-NOW). All other
// channels are ignored by the echo, so I/O CCs (channel 1) never collide
// with echo traffic.
#define TEST_ECHO_CHANNEL 10

// Pins configured over SysEx by the wizard (documentation only — not used by
// this sketch). Digital buttons are INPUT_PULLUP / active low.
#define TEST_DIGITAL_PIN_A 16  // -> CC 16
#define TEST_DIGITAL_PIN_B 17  // -> CC 17
#define TEST_ANALOG_IN_PIN 10  // -> CC 10
#define TEST_ANALOG_OUT_PIN 21 // <- CC 17 (via MIDI loopback)
#define TEST_NOTE_PIN 18       // -> note 60
#define TEST_BEND_PIN 11       // -> pitch bend

// Channel the wizard uses to drive fake inputs (CC n = analog level of pin n,
// note on/off n = press/release pin n, program change = read the pins again).
// Lets the wizard test all inputs without buttons or a potentiometer.
#define TEST_FAKE_CHANNEL 15

// Pins 0..TEST_FAKE_PIN_COUNT-1 can be faked.
#define TEST_FAKE_PIN_COUNT 64

// Program change p on this channel makes the board send API case p with the
// enomik::Client send functions; channel voice messages go out on TEST_API_CHANNEL.
#define TEST_API_TRIGGER_CHANNEL 14
#define TEST_API_CHANNEL 12

// How often (ms) to send a registration handshake to known peers. This lets the
// dongle auto-discover this client in Phase 2 once the dongle MAC has been
// stored via the ADD_PEER SysEx in Phase 1.
#define TEST_HANDSHAKE_INTERVAL_MS 2000
