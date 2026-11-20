# Compile and link the complete libpd component for an ESP-IDF target.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../components/libpd")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(enomik_pd_compile_check)
