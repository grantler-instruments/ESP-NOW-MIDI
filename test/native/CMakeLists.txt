cmake_minimum_required(VERSION 3.16)
project(esp_now_midi_native_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

set(LIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(native_tests test_midi_packet.cpp test_enomik_sysex.cpp test_enomik_sysex_handler.cpp)
target_include_directories(native_tests PRIVATE "${LIB_ROOT}")
target_link_libraries(native_tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(native_tests)
