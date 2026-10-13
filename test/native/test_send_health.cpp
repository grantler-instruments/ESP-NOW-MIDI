// ESP-NOW send health: at most ESP_NOW_MIDI_MAX_IN_FLIGHT unconfirmed packets,
// and a restart when the driver stops confirming sends (it then refuses every
// send with ESP_ERR_ESPNOW_NO_MEM until reboot, seen on hardware under load).

#include <catch2/catch_test_macros.hpp>

#include "arduino_stubs.h"
#include "esp_now_midi.h"

namespace {

const uint8_t kPeer[6] = {0x48, 0xF6, 0xEE, 0x7A, 0x1C, 0xC8};

void start(esp_now_midi &midi)
{
    millisNow() = 1000;
    REQUIRE(midi.begin());
    REQUIRE(midi.addPeer(kPeer));
    stubEspNowSent().clear();
    stubEspNow().absent.clear();
}

const uint8_t kGone[6] = {0x02, 0x50, 0x41, 0x4B, 0x00, 0x01};

void markAbsent(const uint8_t mac[6])
{
    std::array<uint8_t, 6> m;
    memcpy(m.data(), mac, 6);
    stubEspNow().absent.push_back(m);
}

int sentTo(const uint8_t mac[6])
{
    int n = 0;
    for (const auto &p : stubEspNowSent())
    {
        if (memcmp(p.dest, mac, 6) == 0)
        {
            n++;
        }
    }
    return n;
}

} // namespace

TEST_CASE("only one packet waits for its confirmation by default", "[espnow][health]")
{
    // More than one pending send made the driver hang for good on hardware
    REQUIRE(ESP_NOW_MIDI_MAX_IN_FLIGHT == 1);

    uint8_t other[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(other));
    stubEspNow().autoReport = false;

    // To two peers: the second send waits in the queue for the first confirmation
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    REQUIRE(stubEspNowSent().size() == 1);
    REQUIRE(midi.pendingSends() == 1);

    stubEspNowReportSend(ESP_NOW_SEND_SUCCESS);
    midi.service();
    REQUIRE(stubEspNowSent().size() == 2);
    REQUIRE(midi.pendingSends() == 0);

    stubEspNowReportSend(ESP_NOW_SEND_SUCCESS);
    stubEspNow().autoReport = true;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    REQUIRE(stubEspNowSent().size() == 4);
}

TEST_CASE("a healthy driver is never throttled or restarted", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);
    for (int i = 0; i < 2000; i++)
    {
        REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
        if (i % 10 == 0)
            millisNow() += 1;
    }
    millisNow() += 10000;
    midi.service();
    const EspNowSendStats s = midi.getSendStats();
    REQUIRE(s.sent == 2000);
    REQUIRE(s.delivered == 2000);
    REQUIRE(s.throttled == 0);
    REQUIRE(s.restarts == 0);
}

TEST_CASE("sends beyond the in-flight limit wait in the queue until one is confirmed", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false;

    for (int i = 0; i < 3; i++)
        REQUIRE(midi.sendNoteOn(60 + i, 100, 1) == ESP_OK);
    REQUIRE(stubEspNowSent().size() == ESP_NOW_MIDI_MAX_IN_FLIGHT);
    REQUIRE(midi.pendingSends() == 3 - ESP_NOW_MIDI_MAX_IN_FLIGHT);

    for (int i = 0; i < 2; i++)
    {
        stubEspNowReportSend(ESP_NOW_SEND_SUCCESS);
        midi.service();
    }
    REQUIRE(stubEspNowSent().size() == 3);
    for (int i = 0; i < 3; i++)
        REQUIRE(stubEspNowSent()[i].data[1] == 60 + i); // in order
    const EspNowSendStats s = midi.getSendStats();
    REQUIRE(s.throttled == 0);
    REQUIRE(s.dropped == 0);
    REQUIRE(s.queuePeak == 2);
}

TEST_CASE("unconfirmed sends restart ESP-NOW after the stall time", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false;
    const int deinitsBefore = stubEspNow().deinits;

    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    millisNow() += ESP_NOW_MIDI_STALL_MS - 1;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 0);

    millisNow() += 1;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 1);
    REQUIRE(stubEspNow().deinits == deinitsBefore + 1);

    // The peer is registered with the driver again and sending works
    REQUIRE(stubEspNowPeerCount() == 1);
    REQUIRE(midi.getPeersCount() == 1);
    stubEspNowSent().clear();
    REQUIRE(midi.sendNoteOn(61, 100, 1) == ESP_OK);
    REQUIRE(stubEspNowSent().size() == 1);
}

TEST_CASE("a driver that refuses every send is restarted by the next send", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().stalled = true;

    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK); // queued, then refused by the driver
    REQUIRE(midi.getSendStats().failed == 1);
    REQUIRE(midi.getSendStats().lastError == ESP_ERR_ESPNOW_NO_MEM);
    millisNow() += ESP_NOW_MIDI_STALL_MS;
    // The sender checks first, restarts, then sends
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    REQUIRE(midi.getSendStats().restarts == 1);
    REQUIRE(stubEspNowSent().size() == 1);
}

TEST_CASE("an unreachable peer is not a stall", "[espnow][health]")
{
    // The driver still confirms, with "not delivered"
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false;

    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    stubEspNowReportSend(ESP_NOW_SEND_FAIL);
    millisNow() += 10 * ESP_NOW_MIDI_STALL_MS;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 0);
}

TEST_CASE("idle time without sends is not a stall", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);
    millisNow() += 60000;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 0);
}

TEST_CASE("restarts are at least the back-off apart", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);

    stubEspNow().autoReport = false;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    millisNow() += ESP_NOW_MIDI_STALL_MS;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 1);

    // Still broken after the restart
    stubEspNow().autoReport = false;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    millisNow() += ESP_NOW_MIDI_STALL_MS;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 1);

    millisNow() += ESP_NOW_MIDI_RESTART_BACKOFF_MS;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 2);
}

TEST_CASE("receiving still works after a restart", "[espnow][health]")
{
    static int notes = 0;
    notes = 0;
    esp_now_midi midi;
    start(midi);
    midi.setHandleNoteOn([](byte, byte, byte) { notes++; });

    stubEspNow().autoReport = false;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    millisNow() += ESP_NOW_MIDI_STALL_MS;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 1);

    const uint8_t noteOn[3] = {0x90, 60, 100};
    midi.OnDataRecv(kPeer, noteOn, 3);
    REQUIRE(notes == 1);
}

TEST_CASE("a stall that survives the ESP-NOW restart also restarts Wi-Fi", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);
    const int wifiStopsBefore = stubEspNow().wifiStops;
    stubEspNow().deepStalled = true;

    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK); // refused by the driver
    millisNow() += ESP_NOW_MIDI_STALL_MS;
    midi.service(); // 1st: ESP-NOW only, does not help
    REQUIRE(midi.getSendStats().restarts == 1);
    REQUIRE(midi.getSendStats().wifiRestarts == 0);
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    REQUIRE(midi.getSendStats().failed == 2);
    REQUIRE(stubEspNowSent().empty());

    millisNow() += ESP_NOW_MIDI_RESTART_BACKOFF_MS;
    midi.service(); // 2nd: nothing confirmed since the 1st, so Wi-Fi too
    REQUIRE(midi.getSendStats().wifiRestarts == 1);
    REQUIRE(stubEspNow().wifiStops == wifiStopsBefore + 1);
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    REQUIRE(stubEspNowSent().size() == 1);
}

TEST_CASE("a restart that helped keeps the next one ESP-NOW only", "[espnow][health]")
{
    esp_now_midi midi;
    start(midi);

    stubEspNow().autoReport = false;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    millisNow() += ESP_NOW_MIDI_STALL_MS;
    midi.service();
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK); // confirmed again after the restart

    stubEspNow().autoReport = false;
    millisNow() += ESP_NOW_MIDI_RESTART_BACKOFF_MS;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    millisNow() += ESP_NOW_MIDI_STALL_MS;
    midi.service();
    REQUIRE(midi.getSendStats().restarts == 2);
    REQUIRE(midi.getSendStats().wifiRestarts == 0);
}

TEST_CASE("a peer that never acknowledges is skipped after a few sends", "[espnow][health][unreachable]")
{
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(kGone));
    markAbsent(kGone);

    for (int i = 0; i < ESP_NOW_MIDI_UNREACHABLE_AFTER; i++)
    {
        midi.sendNoteOn(60, 100, 1);
    }
    REQUIRE_FALSE(midi.isPeerReachable(kGone));
    REQUIRE(midi.isPeerReachable(kPeer));

    stubEspNowSent().clear();
    for (int i = 0; i < 100; i++)
    {
        REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    }
    REQUIRE(sentTo(kPeer) == 100);
    REQUIRE(sentTo(kGone) == 0);
    REQUIRE(midi.getSendStats().skipped == 100);
}

TEST_CASE("an unreachable peer gets one probe per interval", "[espnow][health][unreachable]")
{
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(kGone));
    markAbsent(kGone);
    for (int i = 0; i < ESP_NOW_MIDI_UNREACHABLE_AFTER; i++)
    {
        midi.sendNoteOn(60, 100, 1);
    }
    stubEspNowSent().clear();

    millisNow() += ESP_NOW_MIDI_PROBE_INTERVAL_MS;
    for (int i = 0; i < 10; i++)
    {
        midi.sendNoteOn(60, 100, 1);
    }
    REQUIRE(sentTo(kGone) == 1);
    // The probe goes after the reachable peer
    REQUIRE(memcmp(stubEspNowSent()[0].dest, kPeer, 6) == 0);
    REQUIRE(memcmp(stubEspNowSent()[1].dest, kGone, 6) == 0);

    millisNow() += ESP_NOW_MIDI_PROBE_INTERVAL_MS;
    midi.sendNoteOn(60, 100, 1);
    REQUIRE(sentTo(kGone) == 2);
}

TEST_CASE("an unreachable peer that acknowledges a probe is reachable again", "[espnow][health][unreachable]")
{
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(kGone));
    markAbsent(kGone);
    for (int i = 0; i < ESP_NOW_MIDI_UNREACHABLE_AFTER; i++)
    {
        midi.sendNoteOn(60, 100, 1);
    }
    REQUIRE_FALSE(midi.isPeerReachable(kGone));

    stubEspNow().absent.clear(); // switched on again
    millisNow() += ESP_NOW_MIDI_PROBE_INTERVAL_MS;
    midi.sendNoteOn(60, 100, 1);
    REQUIRE(midi.isPeerReachable(kGone));
    stubEspNowSent().clear();
    midi.sendNoteOn(60, 100, 1);
    REQUIRE(sentTo(kGone) == 1);
}

TEST_CASE("a packet from an unreachable peer makes it reachable again", "[espnow][health][unreachable]")
{
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(kGone));
    markAbsent(kGone);
    for (int i = 0; i < ESP_NOW_MIDI_UNREACHABLE_AFTER; i++)
    {
        midi.sendNoteOn(60, 100, 1);
    }
    REQUIRE_FALSE(midi.isPeerReachable(kGone));

    const uint8_t noteOn[3] = {0x90, 60, 100};
    midi.OnDataRecv(kGone, noteOn, 3);
    REQUIRE(midi.isPeerReachable(kGone));
}

TEST_CASE("occasional missing acknowledgements keep a peer reachable", "[espnow][health][unreachable]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false;
    for (int i = 0; i < 100; i++)
    {
        REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
        stubEspNowReportSend(i % ESP_NOW_MIDI_UNREACHABLE_AFTER == 0 ? ESP_NOW_SEND_SUCCESS
                                                                     : ESP_NOW_SEND_FAIL);
    }
    REQUIRE(midi.isPeerReachable(kPeer));
    REQUIRE(midi.getSendStats().skipped == 0);
}

TEST_CASE("sending fails when every peer is unreachable and no probe is due", "[espnow][health][unreachable]")
{
    esp_now_midi midi;
    start(midi);
    markAbsent(kPeer);
    for (int i = 0; i < ESP_NOW_MIDI_UNREACHABLE_AFTER; i++)
    {
        REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK); // sent, just not acknowledged
    }
    REQUIRE_FALSE(midi.isPeerReachable(kPeer));
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_FAIL);

    millisNow() += ESP_NOW_MIDI_PROBE_INTERVAL_MS;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK); // the probe
}

TEST_CASE("a re-added peer starts reachable", "[espnow][health][unreachable]")
{
    esp_now_midi midi;
    start(midi);
    markAbsent(kPeer);
    for (int i = 0; i < ESP_NOW_MIDI_UNREACHABLE_AFTER; i++)
    {
        midi.sendNoteOn(60, 100, 1);
    }
    REQUIRE_FALSE(midi.isPeerReachable(kPeer));
    REQUIRE(midi.removePeer(kPeer));
    REQUIRE(midi.addPeer(kPeer));
    REQUIRE(midi.isPeerReachable(kPeer));
}
