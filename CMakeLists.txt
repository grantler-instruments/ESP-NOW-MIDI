# ESP-IDF component (also usable when this repo is added as a component).
# Arduino builds continue to use library.properties and ignore this file.
#
# Only the core public headers under include/ are exposed. Enomik / Arduino-only
# headers at the repo root are intentionally not on the IDF include path.

set(ESP_NOW_MIDI_REQUIRES esp_wifi esp_netif nvs_flash esp_timer driver esp_adc)
# esp_tinyusb backs include/esp_now_midi_usb.h — native USB only (S2/S3).
if(IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND ESP_NOW_MIDI_REQUIRES esp_tinyusb)
endif()

idf_component_register(
    INCLUDE_DIRS "include"
    # driver/esp_adc back include/esp_now_midi_gpio.h (GPIO/PWM/ADC/touch).
    REQUIRES ${ESP_NOW_MIDI_REQUIRES}
)
