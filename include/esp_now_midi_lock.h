/**
 * @file esp_now_midi_lock.h
 * @brief Critical sections shared by the loop task and the ESP-NOW (WiFi) task.
 */
#pragma once

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#else
// Host builds (native tests): a real mutex, so ThreadSanitizer sees the locking.
#include <mutex>
struct portMUX_TYPE
{
  std::mutex m;
};
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) (mux)->m.lock()
#define portEXIT_CRITICAL(mux) (mux)->m.unlock()
#endif

/** @brief Scoped critical section. Keep the body short and free of blocking calls. */
class EspNowMidiLock
{
public:
  explicit EspNowMidiLock(portMUX_TYPE &mux) : _mux(mux) { portENTER_CRITICAL(&_mux); }
  ~EspNowMidiLock() { portEXIT_CRITICAL(&_mux); }
  EspNowMidiLock(const EspNowMidiLock &) = delete;
  EspNowMidiLock &operator=(const EspNowMidiLock &) = delete;

private:
  portMUX_TYPE &_mux;
};
