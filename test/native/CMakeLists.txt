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

add_executable(native_tests
  test_midi_packet.cpp
  test_enomik_sysex.cpp
  test_enomik_sysex_handler.cpp
  test_esp_now_midi_sysex.cpp
  test_peer_mute_list.cpp
  test_peer_storage.cpp
  test_grantler_menu.cpp
  test_usb_midi_queue.cpp
  test_usb_stall_watchdog.cpp
  test_espnow_wire_format.cpp
  test_midi_inbox.cpp
  test_client_dispatch.cpp
  test_io_pitch_bend.cpp
  test_io_outputs.cpp
  test_io_name.cpp
  test_client_peers.cpp
  test_io_readers.cpp
  test_io_pin_config_storage.cpp
  test_io_touch.cpp
  test_announce.cpp
  test_send_health.cpp
  test_send_queue.cpp
  test_display_pacer.cpp
  test_usb_midi_packet.cpp
)
target_include_directories(native_tests PRIVATE
  "${LIB_ROOT}"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)
target_link_libraries(native_tests PRIVATE Catch2::Catch2WithMain)

# Dongle with USB MIDI (TinyUSB stubbed). Its own executable: the Dongle class
# differs with HAS_USB_MIDI, so it must not share a binary with the tests above.
add_executable(native_dongle_usb_tests
  test_dongle_sysex.cpp
)
target_compile_definitions(native_dongle_usb_tests PRIVATE HAS_USB_MIDI)
target_include_directories(native_dongle_usb_tests PRIVATE
  "${LIB_ROOT}"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)
target_link_libraries(native_dongle_usb_tests PRIVATE Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(native_tests)
catch_discover_tests(native_dongle_usb_tests)
