// enomik::IO client name override: stored in NVS and restored after a reboot.

#include <catch2/catch_test_macros.hpp>

#include <cstring>

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
