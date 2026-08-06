# Thin wrapper exposing enomik_dongle.h (repo root - Arduino/Enomik headers
# aren't normally on the IDF include path, see the root CMakeLists.txt) plus
# the core esp_now_midi headers (include/), so this example can reach both.
get_filename_component(ESP_NOW_MIDI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

set(ENOIMK_DONGLE_REQUIRES esp_wifi esp_netif nvs_flash esp_timer)
# esp_tinyusb backs include/esp_now_midi_usb.h - native-USB targets only.
if(IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND ENOIMK_DONGLE_REQUIRES esp_tinyusb)
endif()

idf_component_register(
    INCLUDE_DIRS "${ESP_NOW_MIDI_ROOT}" "${ESP_NOW_MIDI_ROOT}/include"
    REQUIRES ${ENOIMK_DONGLE_REQUIRES}
)
