# ESP-IDF component (also usable when this repo is added as a component).
# Arduino builds continue to use library.properties and ignore this file.
#
# Only the core public headers under include/ are exposed. Enomik / Arduino-only
# headers at the repo root are intentionally not on the IDF include path.

idf_component_register(
    INCLUDE_DIRS "include"
    # esp_tinyusb backs include/esp_now_midi_usb.h (USB MIDI on ESP32-S2/S3).
    # Component name/version depends on the esp_tinyusb release targeted -
    # verify against your IDF version if the build fails to find it.
    REQUIRES esp_wifi esp_netif nvs_flash esp_tinyusb
)
