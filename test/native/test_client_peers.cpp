// enomik::Client peer management: stored peers and live ESP-NOW peers stay in sync.

#include <catch2/catch_test_macros.hpp>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"

namespace {

const uint8_t kPeerA[6] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
const uint8_t kPeerB[6] = {0x11, 0x21, 0x31, 0x41, 0x51, 0x61};

void startClient(enomik::Client &client)
{
    REQUIRE(client.begin());
    stubEspNowSent().clear();
}

// ADD_PEER as the app sends it over SysEx.
void sysexAddPeer(enomik::Client &client, const uint8_t mac[6])
{
    auto pkt = enomik::SysExEncoder::encodeMAC(mac);
    pkt.data[4] = static_cast<uint8_t>(enomik::SysExCommand::ADD_PEER);
    client.io.onSysEx(pkt.data, pkt.length);
}

} // namespace

TEST_CASE("client removePeer stops sending to the peer", "[client][peers]")
{
    enomik::Client client;
    startClient(client);
    REQUIRE(client.addPeer(kPeerA));

    REQUIRE(client.removePeer(kPeerA));
    REQUIRE(client.getPeerCount() == 0);
    REQUIRE_FALSE(client.espnowMIDI.hasPeer(kPeerA));

    stubEspNowSent().clear();
    client.sendNoteOn(60, 100, 1);
    REQUIRE(stubEspNowSent().empty());
}

TEST_CASE("client can re-add a removed peer", "[client][peers]")
{
    enomik::Client client;
    startClient(client);
    REQUIRE(client.addPeer(kPeerA));
    REQUIRE(client.removePeer(kPeerA));

    REQUIRE(client.addPeer(kPeerA));
    REQUIRE(client.getPeerCount() == 1);
    REQUIRE(client.espnowMIDI.hasPeer(kPeerA));
}

TEST_CASE("client removePeer by index removes the live peer", "[client][peers]")
{
    enomik::Client client;
    startClient(client);
    REQUIRE(client.addPeer(kPeerA));
    REQUIRE(client.addPeer(kPeerB));

    REQUIRE(client.removePeer(0));
    REQUIRE_FALSE(client.espnowMIDI.hasPeer(kPeerA));
    REQUIRE(client.espnowMIDI.hasPeer(kPeerB));
    REQUIRE(client.getPeerCount() == 1);
}

TEST_CASE("client removePeer of an unknown peer fails", "[client][peers]")
{
    enomik::Client client;
    startClient(client);
    REQUIRE_FALSE(client.removePeer(kPeerA));
    REQUIRE_FALSE(client.removePeer(nullptr));
}

TEST_CASE("client clearAllPeers clears live peers and allows re-adding", "[client][peers]")
{
    enomik::Client client;
    startClient(client);
    REQUIRE(client.addPeer(kPeerA));
    REQUIRE(client.addPeer(kPeerB));

    client.clearAllPeers();
    REQUIRE(client.getPeerCount() == 0);
    REQUIRE(client.espnowMIDI.getPeersCount() == 0);

    REQUIRE(client.addPeer(kPeerA));
    REQUIRE(client.espnowMIDI.hasPeer(kPeerA));
}

TEST_CASE("client can store an auto-discovered peer", "[client][peers]")
{
    enomik::Client client;
    startClient(client);
    REQUIRE(client.espnowMIDI.addPeer(kPeerA)); // live only, as auto-discovery does

    REQUIRE(client.addPeer(kPeerA));
    REQUIRE(client.getPeerCount() == 1);
    REQUIRE(client.espnowMIDI.hasPeer(kPeerA));
}

TEST_CASE("SysEx ADD_PEER stores an auto-discovered or removed peer", "[client][peers][sysex]")
{
    enomik::Client client;
    startClient(client);

    REQUIRE(client.espnowMIDI.addPeer(kPeerA));
    sysexAddPeer(client, kPeerA);
    REQUIRE(client.getPeerCount() == 1);

    REQUIRE(client.removePeer(kPeerA));
    sysexAddPeer(client, kPeerA);
    REQUIRE(client.getPeerCount() == 1);
    REQUIRE(client.espnowMIDI.hasPeer(kPeerA));
}
