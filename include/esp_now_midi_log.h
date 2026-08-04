/**
 * @file esp_now_midi_log.h
 * @brief Static printf-style logger for Arduino Serial and ESP-IDF ESP_LOG.
 */
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdint>

#ifndef ESP_NOW_DEBUGGING
/** Enable debug-level logs (`EspNowMidiLog::d`) when set to `1`. */
#define ESP_NOW_DEBUGGING 0
#endif

#ifdef ARDUINO
#include <Arduino.h>
#elif defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

/**
 * @brief Framework-agnostic logger used by the library internals.
 *
 * Call sites use printf-style formatting. Backend:
 * - Arduino: `Serial`
 * - Pure ESP-IDF (`ESP_PLATFORM` without `ARDUINO`): `ESP_LOG*`
 * - Host / native tests: no-op
 */
class EspNowMidiLog
{
public:
  static constexpr size_t MAC_STR_LEN = 18; ///< "AA:BB:CC:DD:EE:FF" + NUL

  static void e(const char *fmt, ...)
  {
    va_list args;
    va_start(args, fmt);
    write(Level::Error, fmt, args);
    va_end(args);
  }

  static void w(const char *fmt, ...)
  {
    va_list args;
    va_start(args, fmt);
    write(Level::Warn, fmt, args);
    va_end(args);
  }

  static void i(const char *fmt, ...)
  {
    va_list args;
    va_start(args, fmt);
    write(Level::Info, fmt, args);
    va_end(args);
  }

  static void d(const char *fmt, ...)
  {
#if ESP_NOW_DEBUGGING == 1
    va_list args;
    va_start(args, fmt);
    write(Level::Debug, fmt, args);
    va_end(args);
#else
    (void)fmt;
#endif
  }

  /** @brief Logs `prefix` + uppercase colon-separated MAC at info level. */
  static void mac(const char *prefix, const uint8_t macAddr[6])
  {
    char buf[MAC_STR_LEN];
    formatMac(buf, sizeof(buf), macAddr);
    i("%s%s", prefix ? prefix : "", buf);
  }

  /** @brief Writes `AA:BB:CC:DD:EE:FF` into @p out (uppercase hex). */
  static void formatMac(char *out, size_t outLen, const uint8_t macAddr[6])
  {
    if (!out || outLen == 0)
    {
      return;
    }
    snprintf(out, outLen, "%02X:%02X:%02X:%02X:%02X:%02X",
             macAddr[0], macAddr[1], macAddr[2],
             macAddr[3], macAddr[4], macAddr[5]);
  }

private:
  enum class Level
  {
    Error,
    Warn,
    Info,
    Debug
  };

  static constexpr const char *TAG = "esp_now_midi";

  static void write(Level level, const char *fmt, va_list args)
  {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    char msg[192];
    vsnprintf(msg, sizeof(msg), fmt, args);
#ifdef ARDUINO
    const char *levelTag = "I";
    switch (level)
    {
    case Level::Error:
      levelTag = "E";
      break;
    case Level::Warn:
      levelTag = "W";
      break;
    case Level::Info:
      levelTag = "I";
      break;
    case Level::Debug:
      levelTag = "D";
      break;
    }
    Serial.printf("[%s][%s] %s\n", levelTag, TAG, msg);
#else
    switch (level)
    {
    case Level::Error:
      ESP_LOGE(TAG, "%s", msg);
      break;
    case Level::Warn:
      ESP_LOGW(TAG, "%s", msg);
      break;
    case Level::Info:
      ESP_LOGI(TAG, "%s", msg);
      break;
    case Level::Debug:
      ESP_LOGD(TAG, "%s", msg);
      break;
    }
#endif
#else
    (void)level;
    (void)fmt;
    (void)args;
#endif
  }
};
