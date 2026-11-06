#define HAS_DISPLAY 1

// Optional: default RX playout cushion T (ms) when using setReduceJitterAtCostOfLatency.
// Must be set before including library headers if you override it from a sketch.
// #define ESP_NOW_MIDI_JITTER_BUFFER_MS 8

// Dongle enables jitter reduction by default (timed TX + buffered RX). Set to 0
// for ASAP/raw behavior (needed for peers on firmware older than 0.16.0).
// #define DONGLE_REDUCE_JITTER_AT_COST_OF_LATENCY 0

// SSD1306 OLED (used by SSD1306Display.h when HAS_DISPLAY == 1)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; or run an i2c scanner
#define SPLASH_DURATION_MS 2000
#define HEADER_ALT_INTERVAL_MS 4000
