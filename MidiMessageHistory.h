#pragma once

#include "midiHelpers.h"

/**
 * @brief One entry in the dongle display message history ring buffer.
 */
struct MidiMessageHistory {
  midi_message message;
  unsigned long timestamp;
  bool outgoing;
};
