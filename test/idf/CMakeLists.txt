# Minimal ESP-IDF app to compile-check the esp_now_midi core component.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_now_midi_idf_test)
