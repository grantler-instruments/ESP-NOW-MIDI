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

add_executable(test_midi_packet test_midi_packet.cpp)
target_include_directories(test_midi_packet PRIVATE "${LIB_ROOT}")
target_link_libraries(test_midi_packet PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(test_midi_packet)
