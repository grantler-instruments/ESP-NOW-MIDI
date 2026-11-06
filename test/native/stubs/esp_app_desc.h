#pragma once
// Host stub (native tests): fixed firmware identity.
#include <cstdint>

typedef struct
{
    uint8_t app_elf_sha256[32];
} esp_app_desc_t;

inline const esp_app_desc_t *esp_app_get_description()
{
    static const esp_app_desc_t desc = {{0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18}};
    return &desc;
}
