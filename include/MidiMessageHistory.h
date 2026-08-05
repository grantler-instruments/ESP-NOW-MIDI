#pragma once

#include "./esp_now_midi_helpers.h"

namespace enomik {

/**
 * @brief One entry in the dongle display message history ring buffer.
 */
struct MidiMessageHistory {
  midi_message message;
  unsigned long timestamp;
  bool outgoing;
};

} // namespace enomik
