# Thin wrapper exposing enomik_client.h (repo root) plus include/.
get_filename_component(ESP_NOW_MIDI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

set(ENOIMK_CLIENT_TEST_REQUIRES
    esp_wifi esp_netif nvs_flash esp_timer
    esp_driver_gpio esp_driver_ledc driver esp_adc
)
# esp_tinyusb: include/esp_now_midi_usb.h (USB MIDI for wizard Phase 1)
if(IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND ENOIMK_CLIENT_TEST_REQUIRES esp_tinyusb)
endif()

idf_component_register(
    INCLUDE_DIRS "${ESP_NOW_MIDI_ROOT}" "${ESP_NOW_MIDI_ROOT}/include"
    REQUIRES ${ENOIMK_CLIENT_TEST_REQUIRES}
)
