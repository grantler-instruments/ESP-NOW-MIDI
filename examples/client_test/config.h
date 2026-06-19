#pragma once

// Enable the USB MIDI side of enomik::Client so the wizard can talk to this
// board directly over USB (Phase 1) before testing the ESP-NOW path (Phase 2).
// Requires a USB-capable board (ESP32-S2 / S3 with TinyUSB).
#define HAS_USB_MIDI 1

// Channel the test client echoes back on. Incoming channel-voice messages on
// this channel are mirrored straight back out (USB + ESP-NOW). All other
// channels are ignored by the echo, so button notes (channel 1) never collide
// with echo traffic.
#define TEST_ECHO_CHANNEL 10

// Buttons (INPUT_PULLUP, active low). Configured over SysEx by the wizard.
#define TEST_BUTTON_PIN_A 16
#define TEST_BUTTON_PIN_B 17
#define TEST_BUTTON_PIN_C 9
#define TEST_NOTE_A 60
#define TEST_NOTE_B 61
#define TEST_NOTE_C 62

// How often (ms) to send a registration handshake to known peers. This lets the
// dongle auto-discover this client in Phase 2 once the dongle MAC has been
// stored via the ADD_PEER SysEx in Phase 1.
#define TEST_HANDSHAKE_INTERVAL_MS 2000
