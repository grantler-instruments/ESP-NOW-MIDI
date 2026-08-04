# ESP-IDF component (also usable when this repo is added as a component).
# Arduino builds continue to use library.properties and ignore this file.

idf_component_register(
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif nvs_flash
)
