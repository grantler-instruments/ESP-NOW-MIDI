#pragma once

// Enable the USB MIDI side of enomik::Client so the wizard can talk to this
// board directly over USB (Phase 1) before testing the ESP-NOW path (Phase 2).
// Requires a USB-capable board (ESP32-S2 / S3 with TinyUSB).
#define HAS_USB_MIDI 1

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

// How often (ms) to send a registration handshake to known peers. This lets the
// dongle auto-discover this client in Phase 2 once the dongle MAC has been
// stored via the ADD_PEER SysEx in Phase 1.
#define TEST_HANDSHAKE_INTERVAL_MS 2000
