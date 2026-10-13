// Send queue: sends return right away, the sender sends one packet at a time.
// Without a sender task (host build) service() sends what is queued.

#include <catch2/catch_test_macros.hpp>

#include "arduino_stubs.h"
#include "esp_now_midi.h"

using esp_now_midi_queue::classify;
using esp_now_midi_queue::Priority;

namespace {

const uint8_t kA[6] = {0x48, 0xF6, 0xEE, 0x7A, 0x1C, 0xC8};
const uint8_t kB[6] = {0x48, 0xF6, 0xEE, 0x7A, 0x42, 0xD2};

using SmallQueue = esp_now_midi_queue::SendQueue<16, 2, 32>;

Priority prio(std::initializer_list<uint8_t> bytes)
{
    std::vector<uint8_t> v(bytes);
    return classify(v.data(), v.size());
}

SmallQueue::Result push(SmallQueue &q, const uint8_t mac[6], std::initializer_list<uint8_t> bytes)
{
    std::vector<uint8_t> v(bytes);
    return q.push(mac, v.data(), v.size());
}

std::vector<uint8_t> pop(SmallQueue &q, uint8_t mac[6])
{
    uint8_t buf[32];
    uint8_t flags;
    const size_t len = q.pop(mac, buf, flags);
    return std::vector<uint8_t>(buf, buf + len);
}

void start(esp_now_midi &midi)
{
    millisNow() = 1000;
    REQUIRE(midi.begin());
    REQUIRE(midi.addPeer(kA));
    stubEspNowSent().clear();
    stubEspNow().absent.clear();
}

void confirmAll(esp_now_midi &midi)
{
    for (int i = 0; i < 1000 && (midi.pendingSends() > 0 || i == 0); i++)
    {
        stubEspNowReportSend(ESP_NOW_SEND_SUCCESS);
        midi.service();
    }
    stubEspNowReportSend(ESP_NOW_SEND_SUCCESS);
}

} // namespace

TEST_CASE("note offs and switches matter more than controller values", "[queue]")
{
    REQUIRE(prio({0x80, 60, 0}) == Priority::High);
    REQUIRE(prio({0x90, 60, 0}) == Priority::High); // note on with velocity 0
    REQUIRE(prio({0x90, 60, 100}) == Priority::Normal);
    REQUIRE(prio({0xB0, 64, 0}) == Priority::High);  // sustain
    REQUIRE(prio({0xB0, 123, 0}) == Priority::High); // all notes off
    REQUIRE(prio({0xB0, 7, 100}) == Priority::Low);
    REQUIRE(prio({0xB0, 0, 1}) == Priority::Normal); // bank select keeps its order
    REQUIRE(prio({0xE0, 0, 64}) == Priority::Low);
    REQUIRE(prio({0xD0, 10}) == Priority::Low);
    REQUIRE(prio({0xC0, 5}) == Priority::Normal);
    REQUIRE(prio({0xF8}) == Priority::Low);
    REQUIRE(prio({0xFC}) == Priority::High);
}

TEST_CASE("a new controller value replaces a waiting one for the same peer", "[queue]")
{
    SmallQueue q;
    REQUIRE(push(q, kA, {0xB0, 7, 10}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0x90, 60, 100}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0xB0, 7, 20}) == SmallQueue::Result::Replaced);
    REQUIRE(push(q, kA, {0xB1, 7, 30}) == SmallQueue::Result::Queued); // other channel
    REQUIRE(push(q, kB, {0xB0, 7, 40}) == SmallQueue::Result::Queued); // other peer
    REQUIRE(push(q, kA, {0xB0, 8, 50}) == SmallQueue::Result::Queued); // other controller
    REQUIRE(push(q, kA, {0xE0, 0, 64}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0xE0, 0, 70}) == SmallQueue::Result::Replaced);
    REQUIRE(q.size() == 6);

    uint8_t mac[6];
    REQUIRE(pop(q, mac) == std::vector<uint8_t>{0xB0, 7, 20});
    REQUIRE(pop(q, mac) == std::vector<uint8_t>{0x90, 60, 100});
}

TEST_CASE("switches and ordered controllers are never replaced", "[queue]")
{
    SmallQueue q;
    REQUIRE(push(q, kA, {0xB0, 64, 127}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0xB0, 64, 0}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0xB0, 6, 1}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0xB0, 6, 2}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0x90, 60, 100}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0x90, 60, 100}) == SmallQueue::Result::Queued);
    REQUIRE(q.size() == 6);
}

TEST_CASE("a filling queue refuses controller values first and note offs last", "[queue]")
{
    SmallQueue q; // 16 slots: Low up to 8, Normal up to 14, High up to 16
    for (uint8_t i = 0; i < 8; i++)
        REQUIRE(push(q, kA, {0xB0, static_cast<uint8_t>(10 + i), 1}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0xB0, 30, 1}) == SmallQueue::Result::Full);
    REQUIRE(push(q, kA, {0xB0, 10, 2}) == SmallQueue::Result::Replaced); // still updates
    for (uint8_t i = 0; i < 6; i++)
        REQUIRE(push(q, kA, {0x90, static_cast<uint8_t>(60 + i), 100}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0x90, 70, 100}) == SmallQueue::Result::Full);
    REQUIRE(push(q, kA, {0x80, 60, 0}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0x80, 61, 0}) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0x80, 62, 0}) == SmallQueue::Result::Full);
    REQUIRE(q.size() == 16);
}

TEST_CASE("short messages and frames come out in the order they went in", "[queue]")
{
    SmallQueue q;
    std::vector<uint8_t> frame(20, 0x11);
    REQUIRE(push(q, kA, {0x90, 60, 100}) == SmallQueue::Result::Queued);
    REQUIRE(q.push(kA, frame.data(), frame.size()) == SmallQueue::Result::Queued);
    REQUIRE(push(q, kA, {0x80, 60, 0}) == SmallQueue::Result::Queued);
    frame[0] = 0x22;
    REQUIRE(q.push(kB, frame.data(), frame.size()) == SmallQueue::Result::Queued);
    REQUIRE(q.push(kB, frame.data(), frame.size()) == SmallQueue::Result::Full); // 2 frame slots
    std::vector<uint8_t> tooLong(33, 0);
    REQUIRE(q.push(kA, tooLong.data(), tooLong.size()) == SmallQueue::Result::TooLong);

    uint8_t mac[6];
    REQUIRE(pop(q, mac).size() == 3);
    REQUIRE(pop(q, mac)[0] == 0x11);
    REQUIRE(pop(q, mac) == std::vector<uint8_t>{0x80, 60, 0});
    std::vector<uint8_t> last = pop(q, mac);
    REQUIRE(last[0] == 0x22);
    REQUIRE(memcmp(mac, kB, 6) == 0);
    REQUIRE(pop(q, mac).empty());
}

TEST_CASE("a burst is queued whole and sent in order as confirmations arrive", "[queue][espnow]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false; // like a driver that confirms later

    for (int i = 0; i < 100; i++)
        REQUIRE(midi.sendNoteOn(i, 100, 1) == ESP_OK);
    REQUIRE(stubEspNowSent().size() == 1);
    REQUIRE(midi.pendingSends() == 99);

    confirmAll(midi);
    REQUIRE(stubEspNowSent().size() == 100);
    for (int i = 0; i < 100; i++)
        REQUIRE(stubEspNowSent()[i].data[1] == i);
    const EspNowSendStats s = midi.getSendStats();
    REQUIRE(s.sent == 100);
    REQUIRE(s.dropped == 0);
    REQUIRE(s.failed == 0);
}

TEST_CASE("a full queue drops and counts, but a note off still fits", "[queue][espnow]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false;

    int refused = 0;
    for (int i = 0; i < 200; i++)
    {
        if (midi.sendNoteOn(i % 128, 100, 1) != ESP_OK)
            refused++;
    }
    REQUIRE(refused > 0);
    REQUIRE(midi.sendNoteOff(60, 0, 1) == ESP_OK);

    const EspNowSendStats s = midi.getSendStats();
    REQUIRE(s.dropped == static_cast<uint32_t>(refused));
    REQUIRE(s.lastError == ESP_ERR_ESPNOW_NO_MEM);
    REQUIRE(s.queuePeak >= ESP_NOW_MIDI_QUEUE_SHORT - ESP_NOW_MIDI_QUEUE_SHORT / 8);
}

TEST_CASE("a moving knob does not fill the queue", "[queue][espnow]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false;

    for (int v = 0; v < 500; v++)
        REQUIRE(midi.sendControlChange(7, v % 128, 1) == ESP_OK);
    REQUIRE(midi.pendingSends() == 1); // one sent, one waiting with the newest value
    confirmAll(midi);
    REQUIRE(stubEspNowSent().size() == 2);
    REQUIRE(stubEspNowSent()[1].data[2] == 499 % 128);
    REQUIRE(midi.getSendStats().replaced == 498);
}

TEST_CASE("queued packets for a peer that became unreachable are skipped", "[queue][espnow][unreachable]")
{
    const uint8_t gone[6] = {0x02, 0x50, 0x41, 0x4B, 0x00, 0x01};
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(gone));
    std::array<uint8_t, 6> m;
    memcpy(m.data(), gone, 6);
    stubEspNow().absent.push_back(m);
    stubEspNow().autoReport = false;

    // Queued for both while the missing peer still counts as reachable
    for (int i = 0; i < 20; i++)
        REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    stubEspNow().autoReport = true;
    for (int i = 0; i < 60 && midi.pendingSends() > 0; i++)
    {
        stubEspNowReportSend(memcmp(stubEspNow().lastDest, gone, 6) == 0 ? ESP_NOW_SEND_FAIL
                                                                           : ESP_NOW_SEND_SUCCESS);
        midi.service();
    }
    REQUIRE(midi.pendingSends() == 0);
    REQUIRE_FALSE(midi.isPeerReachable(gone));

    int toGone = 0;
    int toA = 0;
    for (const auto &p : stubEspNowSent())
        (memcmp(p.dest, gone, 6) == 0 ? toGone : toA)++;
    REQUIRE(toA == 20);
    REQUIRE(toGone == ESP_NOW_MIDI_UNREACHABLE_AFTER);
    REQUIRE(midi.getSendStats().skipped == 20 - ESP_NOW_MIDI_UNREACHABLE_AFTER);
}

TEST_CASE("queued packets for a removed peer are left out", "[queue][espnow]")
{
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(kB));
    stubEspNow().autoReport = false;
    for (int i = 0; i < 5; i++)
        REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    REQUIRE(midi.removePeer(kB));
    confirmAll(midi);

    // The first packet (to kA) was in flight; nothing reached kB
    REQUIRE(stubEspNowSent().size() == 5);
    for (const auto &p : stubEspNowSent())
        REQUIRE(memcmp(p.dest, kA, 6) == 0);
    REQUIRE(midi.getSendStats().failed == 0);
}

TEST_CASE("a SysEx with more frames than the queue holds still goes out whole", "[queue][espnow][sysex]")
{
    esp_now_midi midi;
    start(midi);
    REQUIRE(midi.addPeer(kB));

    std::vector<uint8_t> sysex(1000, 0x42);
    sysex.front() = 0xF0;
    sysex.back() = 0xF7;
    const uint8_t frames = esp_now_midi_sysex::fragmentCount(sysex.size());
    REQUIRE(frames * 2 > ESP_NOW_MIDI_QUEUE_FRAMES);
    REQUIRE(midi.sendSysex(sysex.data(), sysex.size()) == ESP_OK);
    REQUIRE(midi.pendingSends() == 0);
    REQUIRE(stubEspNowSent().size() == frames * 2u);
}

TEST_CASE("flush sends what is queued", "[queue][espnow]")
{
    esp_now_midi midi;
    start(midi);
    stubEspNow().autoReport = false;
    REQUIRE(midi.sendNoteOn(60, 100, 1) == ESP_OK);
    REQUIRE(midi.sendNoteOff(60, 0, 1) == ESP_OK);
    REQUIRE_FALSE(midi.flush(10)); // nothing confirms

    stubEspNow().autoReport = true;
    stubEspNowReportSend(ESP_NOW_SEND_SUCCESS);
    REQUIRE(midi.flush(10));
    REQUIRE(stubEspNowSent().size() == 2);
}

TEST_CASE("a send to an unknown peer is refused right away", "[queue][espnow]")
{
    esp_now_midi midi;
    start(midi);
    const uint8_t data[3] = {0x90, 60, 100};
    REQUIRE(midi.send(kB, data, 3) == ESP_ERR_ESPNOW_NOT_FOUND);
    REQUIRE(midi.send(kA, data, 3) == ESP_OK);
    REQUIRE(stubEspNowSent().size() == 1);
}
