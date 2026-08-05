# Thin wrapper so the repo registers as component `esp_now_midi` and only
# exposes the core public headers (include/), not Arduino-only Enomik headers.
get_filename_component(ESP_NOW_MIDI_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../../../include" ABSOLUTE)

idf_component_register(
    INCLUDE_DIRS "${ESP_NOW_MIDI_INCLUDE}"
    # driver/esp_adc back include/esp_now_midi_gpio.h (GPIO/PWM/ADC/touch),
    # unused by this smoke test's main.cpp today but part of the exposed
    # include/ surface - kept in sync with the root CMakeLists.txt.
    REQUIRES esp_wifi esp_netif nvs_flash esp_timer driver esp_adc
)
