#pragma once

// PN532 on I2C. The Adafruit library polls the RDY byte over I2C, so the IRQ
// pin does not have to be wired. RESET is optional too (-1 = not connected).
#define PN532_IRQ_PIN -1
#define PN532_RESET_PIN -1

// Leave at -1 to use the board's default I2C pins.
#define NFC_I2C_SDA_PIN -1
#define NFC_I2C_SCL_PIN -1

// Reader timing. Each poll starts one card detection, waits
// NFC_RESPONSE_WAIT_MS for the reader's answer, then reads it.
#define NFC_POLL_INTERVAL_MS 100
#define NFC_RESPONSE_WAIT_MS 40

// A tag counts as removed after this many polls in a row without it.
#define NFC_REMOVE_MISSES 3

// Maximum number of tag -> MIDI mappings stored in flash.
#define NFC_MAX_MAPPINGS 32

// Send note off when a tag mapped to a note on is removed.
#define NFC_NOTE_OFF_ON_REMOVE 1
