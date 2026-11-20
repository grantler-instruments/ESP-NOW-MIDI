# enomik client running Pure Data (libpd) on the Waveshare ESP32-S3-AUDIO-Board
# (ES8311 DAC + ES7210 mics). The Pd runtime and the board's audio backend are
# in the enomik_pd component (../../components). Pd patches live in pd/ and
# are flashed as a LittleFS image.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_now_midi_client_pd_waveshare_audio_s3)
