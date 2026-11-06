# client_test DUT for scripts/wizard — pure ESP-IDF port of examples/client_test.
# Requires a native-USB target (ESP32-S2 / ESP32-S3) for the wizard's USB MIDI path.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_now_midi_client_test_example)
