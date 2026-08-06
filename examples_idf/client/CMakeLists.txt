# ESP-NOW MIDI client, built against pure ESP-IDF (no Arduino).
# Port of examples/client. Works on any ESP32 with Wi-Fi; optional USB MIDI
# on native-USB targets (ESP32-S2 / ESP32-S3) via HAS_USB_MIDI.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_now_midi_client_example)
