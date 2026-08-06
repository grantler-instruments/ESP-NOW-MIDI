# USB MIDI <-> ESP-NOW dongle, built against pure ESP-IDF (no Arduino).
# Requires a native-USB target (ESP32-S2 / ESP32-S3).
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_now_midi_dongle_example)
