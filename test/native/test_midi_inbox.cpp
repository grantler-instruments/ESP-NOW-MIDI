#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <thread>

#include "include/MidiInbox.h"

namespace {

midi_message note(uint8_t n)
{
    midi_message m{};
    m.channel = 1;
    m.status = MIDI_NOTE_ON;
    m.firstByte = n;
    m.secondByte = 100;
    return m;
}

const uint8_t kMac[6] = {1, 2, 3, 4, 5, 6};

} // namespace

TEST_CASE("MidiInbox delivers messages and senders in order", "[inbox]")
{
    MidiInbox inbox;
    midi_message m;
    uint8_t mac[6];
    REQUIRE_FALSE(inbox.pop(m, mac));

    for (uint8_t round = 0; round < 3; ++round) // wraps around the ring
    {
        for (uint8_t i = 0; i < 40; ++i)
        {
            REQUIRE(inbox.push(note(i), kMac));
        }
        for (uint8_t i = 0; i < 40; ++i)
        {
            REQUIRE(inbox.pop(m, mac));
            REQUIRE(m.firstByte == i);
            REQUIRE(memcmp(mac, kMac, 6) == 0);
        }
    }
    REQUIRE_FALSE(inbox.pop(m, mac));
}

TEST_CASE("MidiInbox drops and counts when full", "[inbox]")
{
    MidiInbox inbox;
    for (int i = 0; i < MIDI_INBOX_SIZE - 1; ++i)
    {
        REQUIRE(inbox.push(note(static_cast<uint8_t>(i)), kMac));
    }
    REQUIRE_FALSE(inbox.push(note(99), kMac));
    REQUIRE(inbox.droppedCount() == 1);

    midi_message m;
    uint8_t mac[6];
    REQUIRE(inbox.pop(m, mac));
    REQUIRE(m.firstByte == 0);
    REQUIRE(inbox.push(note(100), nullptr));
}

TEST_CASE("MidiInbox is safe between a producer and a consumer thread", "[inbox][threads]")
{
    MidiInbox inbox;
    std::thread wifi([&]() {
        for (int i = 0; i < 20000; ++i)
        {
            while (!inbox.push(note(static_cast<uint8_t>(i & 0x7F)), kMac))
            {
                std::this_thread::yield();
            }
        }
    });

    int received = 0;
    midi_message m;
    uint8_t mac[6];
    while (received < 20000)
    {
        if (inbox.pop(m, mac))
        {
            REQUIRE(m.firstByte == (received & 0x7F));
            ++received;
        }
        else
        {
            std::this_thread::yield();
        }
    }
    wifi.join();
    REQUIRE(received == 20000);
}
