// enomik::IO client name override: stored in NVS and restored after a reboot.

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"

TEST_CASE("client name override survives a reboot", "[io][name]")
{
    stubPrefsPersist() = true;
    stubPrefsBlobs().clear();

    {
        enomik::IO io;
        io.begin(); // first boot: stores the firmware id
        io.setNameOverride("studio-left");
        REQUIRE(strcmp(io.getName(), "studio-left") == 0);
    }

    enomik::IO rebooted;
    rebooted.begin();
    REQUIRE(strcmp(rebooted.getName(), "studio-left") == 0);

    stubPrefsPersist() = false;
    stubPrefsBlobs().clear();
}

TEST_CASE("client name override of maximum length survives a reboot", "[io][name]")
{
    stubPrefsPersist() = true;
    stubPrefsBlobs().clear();

    const char *name = "sixteen-chars-ok"; // MAX_NAME_LENGTH
    REQUIRE(strlen(name) == enomik::MAX_NAME_LENGTH);
    {
        enomik::IO io;
        io.begin();
        io.setNameOverride(name);
    }

    enomik::IO rebooted;
    rebooted.begin();
    REQUIRE(strcmp(rebooted.getName(), name) == 0);

    stubPrefsPersist() = false;
    stubPrefsBlobs().clear();
}

TEST_CASE("USB serial number is the MAC plus the sketch name", "[io][name][usb]")
{
    const uint8_t mac[6] = {0x48, 0xF6, 0xEE, 0x7A, 0x42, 0xD2};
    char serial[64];

    enomik::formatUsbSerial(serial, sizeof(serial), mac, "client_test");
    REQUIRE(std::string(serial) == "48F6EE7A42D2-client_test");

    enomik::formatUsbSerial(serial, sizeof(serial), mac, nullptr);
    REQUIRE(std::string(serial) == "48F6EE7A42D2");

    enomik::formatUsbSerial(serial, sizeof(serial), mac, "");
    REQUIRE(std::string(serial) == "48F6EE7A42D2");

    // Characters Windows device IDs refuse become '_'
    enomik::formatUsbSerial(serial, sizeof(serial), mac, "my synth, v2&x");
    REQUIRE(std::string(serial) == "48F6EE7A42D2-my_synth__v2_x");

    // Never longer than a USB string descriptor holds (32), and never past the buffer
    enomik::formatUsbSerial(serial, sizeof(serial), mac, "abcdefghijklmnopqrstuvwxyz");
    REQUIRE(std::string(serial) == "48F6EE7A42D2-abcdefghijklmnopqrs");
    REQUIRE(std::string(serial).size() == enomik::USB_SERIAL_MAX_LENGTH);
    char small[16];
    enomik::formatUsbSerial(small, sizeof(small), mac, "client_test");
    REQUIRE(std::string(small) == "48F6EE7A42D2-cl");
}

TEST_CASE("client keeps the sketch name separate from the app override", "[io][name]")
{
    enomik::IO io;
    io.setDefaultName("client_test");
    io.setNameOverride("stage left");
    REQUIRE(std::string(io.getName()) == "stage left");
    REQUIRE(std::string(io.getDefaultName()) == "client_test");
}
