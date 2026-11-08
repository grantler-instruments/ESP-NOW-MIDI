#pragma once

// PN532 on I2C. The Adafruit library polls the RDY byte over I2C, so the IRQ
// pin does not have to be wired. RESET is optional too (-1 = not connected).
#define PN532_IRQ_PIN -1
#define PN532_RESET_PIN -1

// Leave at -1 to use the board's default I2C pins.
#define NFC_I2C_SDA_PIN -1
#define NFC_I2C_SCL_PIN -1

// I2C clock. The PN532 supports up to 400 kHz, which shortens each poll (the
// detection answer takes ~2 ms at 100 kHz). Drop back to 100000 with long wires.
#define NFC_I2C_CLOCK_HZ 100000

// Reader timing. Each poll starts one card detection and reads the answer as soon
// as the reader is ready, giving up after NFC_RESPONSE_TIMEOUT_MS.
#define NFC_POLL_INTERVAL_MS 100
#define NFC_RESPONSE_TIMEOUT_MS 200

// A tag counts as removed after this many polls in a row without it.
#define NFC_REMOVE_MISSES 3

// Restart the reader after this many detection starts in a row got no ACK.
#define NFC_MAX_START_FAILS 5

// If the PN532 does not answer at startup, try again this often (logs each try).
#define NFC_RETRY_INTERVAL_MS 5000

// Serial logging of this sketch (library logs are separate):
//   0 = off
//   1 = reader setup and errors (pins, bus scan, wiring hints)
//   2 = + tag placed / removed, mapping events
//   3 = + reader statistics every NFC_STATS_INTERVAL_MS
// Turn it up while wiring the reader; 0 keeps the loop free of serial output.
#define NFC_LOG_LEVEL 0
#define NFC_STATS_INTERVAL_MS 5000

// Maximum number of tag -> MIDI mappings stored in flash.
#define NFC_MAX_MAPPINGS 32

// Send note off when a tag mapped to a note on is removed.
#define NFC_NOTE_OFF_ON_REMOVE 1
