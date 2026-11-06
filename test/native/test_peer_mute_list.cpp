#include <catch2/catch_test_macros.hpp>

#include "include/PeerMuteList.h"

namespace {

constexpr uint8_t kMacA[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
constexpr uint8_t kMacB[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

} // namespace

TEST_CASE("PeerMuteList starts empty", "[dongle][mute]")
{
    enomik::PeerMuteList mutes;
    REQUIRE(mutes.count() == 0);
    REQUIRE_FALSE(mutes.contains(kMacA));
    REQUIRE_FALSE(mutes.contains(nullptr));
}

TEST_CASE("PeerMuteList mutes and unmutes by MAC", "[dongle][mute]")
{
    enomik::PeerMuteList mutes;

    REQUIRE(mutes.set(kMacA, true));
    REQUIRE(mutes.contains(kMacA));
    REQUIRE_FALSE(mutes.contains(kMacB));
    REQUIRE(mutes.count() == 1);

    REQUIRE(mutes.set(kMacA, true));
    REQUIRE(mutes.count() == 1);

    REQUIRE(mutes.set(kMacA, false));
    REQUIRE_FALSE(mutes.contains(kMacA));
    REQUIRE(mutes.count() == 0);

    REQUIRE(mutes.set(kMacB, false));
    REQUIRE_FALSE(mutes.contains(kMacB));
}

TEST_CASE("PeerMuteList rejects a null MAC", "[dongle][mute]")
{
    enomik::PeerMuteList mutes;
    REQUIRE_FALSE(mutes.set(nullptr, true));
    mutes.clear(nullptr);
}

TEST_CASE("PeerMuteList clear removes one MAC", "[dongle][mute]")
{
    enomik::PeerMuteList mutes;
    REQUIRE(mutes.set(kMacA, true));
    REQUIRE(mutes.set(kMacB, true));

    mutes.clear(kMacA);
    REQUIRE_FALSE(mutes.contains(kMacA));
    REQUIRE(mutes.contains(kMacB));
    REQUIRE(mutes.count() == 1);

    mutes.clear(kMacA);
    REQUIRE(mutes.count() == 1);
}
