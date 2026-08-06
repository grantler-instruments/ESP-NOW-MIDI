#pragma once

#ifdef ARDUINO
#include <Preferences.h>
#elif defined(ESP_PLATFORM)
#include "nvs.h"
#include "nvs_flash.h"
#include "./esp_now_midi_log.h"
#include <cstdint>
#include <cstddef>
#endif

namespace enomik
{

#ifdef ARDUINO

using Preferences = ::Preferences;

#elif defined(ESP_PLATFORM)

/**
 * @brief Framework-agnostic subset of Arduino's `Preferences` API, backed by NVS.
 *
 * Mirrors only the methods enomik_io.h actually uses.
 */
class Preferences
{
public:
    bool begin(const char *name, bool readOnly = false)
    {
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            nvs_flash_erase();
            err = nvs_flash_init();
        }
        if (err != ESP_OK)
        {
            EspNowMidiLog::e("Preferences: nvs_flash_init failed: %d", static_cast<int>(err));
            return false;
        }

        err = nvs_open(name, readOnly ? NVS_READONLY : NVS_READWRITE, &_handle);
        _open = (err == ESP_OK);
        if (!_open && err != ESP_ERR_NVS_NOT_FOUND)
        {
            EspNowMidiLog::e("Preferences: nvs_open failed: %d", static_cast<int>(err));
        }
        return _open;
    }

    void end()
    {
        if (_open)
        {
            nvs_close(_handle);
            _open = false;
        }
    }

    void clear()
    {
        if (!_open)
        {
            return;
        }
        nvs_erase_all(_handle);
        nvs_commit(_handle);
    }

    size_t putBytes(const char *key, const void *value, size_t len)
    {
        if (!_open)
        {
            return 0;
        }
        if (nvs_set_blob(_handle, key, value, len) != ESP_OK)
        {
            return 0;
        }
        nvs_commit(_handle);
        return len;
    }

    size_t getBytes(const char *key, void *buf, size_t maxLen)
    {
        if (!_open)
        {
            return 0;
        }
        size_t length = maxLen;
        if (nvs_get_blob(_handle, key, buf, &length) != ESP_OK)
        {
            return 0;
        }
        return length;
    }

    size_t putUInt(const char *key, uint32_t value)
    {
        if (!_open)
        {
            return 0;
        }
        if (nvs_set_u32(_handle, key, value) != ESP_OK)
        {
            return 0;
        }
        nvs_commit(_handle);
        return sizeof(value);
    }

    uint32_t getUInt(const char *key, uint32_t defaultValue = 0)
    {
        if (!_open)
        {
            return defaultValue;
        }
        uint32_t value = defaultValue;
        if (nvs_get_u32(_handle, key, &value) != ESP_OK)
        {
            return defaultValue;
        }
        return value;
    }

    size_t putUChar(const char *key, uint8_t value)
    {
        if (!_open)
        {
            return 0;
        }
        if (nvs_set_u8(_handle, key, value) != ESP_OK)
        {
            return 0;
        }
        nvs_commit(_handle);
        return sizeof(value);
    }

    uint8_t getUChar(const char *key, uint8_t defaultValue = 0)
    {
        if (!_open)
        {
            return defaultValue;
        }
        uint8_t value = defaultValue;
        if (nvs_get_u8(_handle, key, &value) != ESP_OK)
        {
            return defaultValue;
        }
        return value;
    }

private:
    nvs_handle_t _handle = 0;
    bool _open = false;
};

#endif

} // namespace enomik
