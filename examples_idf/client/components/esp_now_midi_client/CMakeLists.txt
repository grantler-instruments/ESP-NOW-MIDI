# Thin wrapper exposing enomik_client.h (repo root - Arduino/Enomik headers
# aren't normally on the IDF include path) plus the core esp_now_midi headers
# (include/), so this example can reach both.
get_filename_component(ESP_NOW_MIDI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

idf_component_register(
    INCLUDE_DIRS "${ESP_NOW_MIDI_ROOT}" "${ESP_NOW_MIDI_ROOT}/include"
    # esp_tinyusb backs include/esp_now_midi_usb.h when HAS_USB_MIDI is set (S2/S3).
    # esp_driver_* / driver: include/esp_now_midi_gpio.h (GPIO/PWM/ADC/touch)
    # esp_tinyusb: include/esp_now_midi_usb.h when HAS_USB_MIDI (S2/S3)
    REQUIRES esp_wifi esp_netif nvs_flash esp_timer esp_tinyusb
             esp_driver_gpio esp_driver_ledc driver esp_adc
)
