# Thin wrapper so the repo registers as component `esp_now_midi` and only
# exposes the core public headers (include/), not Arduino-only Enomik headers.
get_filename_component(ESP_NOW_MIDI_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../../../../include" ABSOLUTE)

idf_component_register(
    INCLUDE_DIRS "${ESP_NOW_MIDI_INCLUDE}"
    REQUIRES esp_wifi esp_netif nvs_flash esp_timer
)
