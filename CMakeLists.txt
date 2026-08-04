# ESP-IDF component (also usable when this repo is added as a component).
# Arduino builds continue to use library.properties and ignore this file.
#
# Only the core public headers under include/ are exposed. Enomik / Arduino-only
# headers at the repo root are intentionally not on the IDF include path.

idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif nvs_flash esp_timer
)
