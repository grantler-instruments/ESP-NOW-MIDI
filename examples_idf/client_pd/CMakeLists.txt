# enomik client running Pure Data (libpd) with generic I2S audio.
# The Pd runtime is the enomik_pd component (../../components); this example
# only picks the audio backend. Pd patches live in pd/ and are flashed as a
# LittleFS image.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_now_midi_client_pd_example)
