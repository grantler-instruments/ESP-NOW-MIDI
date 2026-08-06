# Thin wrapper exposing enomik_client.h (repo root) plus include/.
get_filename_component(ESP_NOW_MIDI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

idf_component_register(
    INCLUDE_DIRS "${ESP_NOW_MIDI_ROOT}" "${ESP_NOW_MIDI_ROOT}/include"
    # esp_driver_* / driver: include/esp_now_midi_gpio.h (GPIO/PWM/ADC/touch)
    # esp_tinyusb: include/esp_now_midi_usb.h (USB MIDI for wizard Phase 1)
    REQUIRES esp_wifi esp_netif nvs_flash esp_timer esp_tinyusb
             esp_driver_gpio esp_driver_ledc driver esp_adc
)
