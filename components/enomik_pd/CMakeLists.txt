# enomik_pd: run a Pure Data patch (libpd) as an enomik client.
#
# Use from an IDF project by adding this repository's components/ folder:
#   list(APPEND EXTRA_COMPONENT_DIRS "<path to ESP-NOW-MIDI>/components")
# and `REQUIRES enomik_pd` in your component. See README.md.
#
# Needs the full ESP-NOW-MIDI checkout (git clone, submodule or local path):
# enomik_client.h and include/ come from the repository root.

get_filename_component(ESP_NOW_MIDI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(srcs
    "src/enomik_pd.cpp"
    "src/midi_bridge.cpp"
    "src/patch_upload.cpp"
    "src/audio_i2s_std.cpp"
)
set(requires
    libpd esp_driver_i2s esp_timer pthread
    # enomik::Client (header-only, repo root)
    esp_wifi esp_netif nvs_flash esp_driver_gpio esp_driver_ledc driver esp_adc
)
set(priv_requires)

# esp_tinyusb backs include/esp_now_midi_usb.h (USB MIDI) on native-USB targets.
if(IDF_TARGET STREQUAL "esp32s2" OR IDF_TARGET STREQUAL "esp32s3")
    list(APPEND requires esp_tinyusb)
endif()

# Waveshare ESP32-S3-AUDIO-Board backend (ES8311/ES7210 via esp_codec_dev).
if(IDF_TARGET STREQUAL "esp32s3")
    list(APPEND srcs "src/audio_waveshare_s3.cpp")
    list(APPEND priv_requires esp_driver_i2c espressif__esp_codec_dev)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include" "${ESP_NOW_MIDI_ROOT}" "${ESP_NOW_MIDI_ROOT}/include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES ${requires}
    PRIV_REQUIRES ${priv_requires} joltwallet__littlefs
)

# Debug aid: `idf.py -D ENOMIK_NO_USB_MIDI=1 build` keeps native USB as the
# serial console (no USB MIDI), so logs and panics stay visible on S2/S3.
if(ENOMIK_NO_USB_MIDI)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC ENOMIK_NO_USB_MIDI)
endif()
