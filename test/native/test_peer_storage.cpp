#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "include/PeerStorage.h"

namespace {

constexpr uint8_t kMacA[6] = {0x84, 0xF7, 0x03, 0xF2, 0x54, 0x62};
constexpr uint8_t kMacB[6] = {0x84, 0xF7, 0x03, 0xF2, 0x54, 0x63};

} // namespace

TEST_CASE("PeerStorage add and lookup", "[dongle][persist]")
{
    enomik::PeerStorage store;
    REQUIRE(store.begin());
    REQUIRE(store.isEmpty());

    REQUIRE(store.add(kMacA));
    REQUIRE(store.exists(kMacA));
    REQUIRE_FALSE(store.exists(kMacB));
    REQUIRE(store.count() == 1);
    REQUIRE(memcmp(store.get(0), kMacA, 6) == 0);
    REQUIRE(store.get(1) == nullptr);
}

TEST_CASE("PeerStorage rejects duplicates", "[dongle][persist]")
{
    enomik::PeerStorage store;
    REQUIRE(store.begin());
    REQUIRE(store.add(kMacA));
    REQUIRE_FALSE(store.add(kMacA));
    REQUIRE(store.count() == 1);
}

TEST_CASE("PeerStorage add is a no-op before begin", "[dongle][persist]")
{
    enomik::PeerStorage store;
    REQUIRE_FALSE(store.add(kMacA));
    REQUIRE(store.count() == 0);
}

TEST_CASE("PeerStorage remove and clear", "[dongle][persist]")
{
    enomik::PeerStorage store;
    REQUIRE(store.begin());
    REQUIRE(store.add(kMacA));
    REQUIRE(store.add(kMacB));

    REQUIRE(store.remove(kMacA));
    REQUIRE_FALSE(store.exists(kMacA));
    REQUIRE(store.exists(kMacB));
    REQUIRE(store.count() == 1);

    REQUIRE_FALSE(store.remove(kMacA));
    store.clear();
    REQUIRE(store.isEmpty());
}

TEST_CASE("PeerStorage fills to MAX_PEERS", "[dongle][persist]")
{
    enomik::PeerStorage store;
    REQUIRE(store.begin());

    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < MAX_PEERS; i++)
    {
        mac[5] = static_cast<uint8_t>(i + 1);
        REQUIRE(store.add(mac));
    }
    REQUIRE(store.isFull());
    mac[5] = 0xFF;
    REQUIRE_FALSE(store.add(mac));
}

TEST_CASE("PeerStorage RAM backend survives a second begin on the same instance", "[dongle][persist]")
{
    enomik::PeerStorage store;
    REQUIRE(store.begin());
    REQUIRE(store.add(kMacA));
    REQUIRE(store.begin());
    REQUIRE(store.exists(kMacA));
    REQUIRE(store.count() == 1);
}
