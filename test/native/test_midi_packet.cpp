#include <catch2/catch_test_macros.hpp>

#include "esp_now_midi_helpers.h"

namespace {

midi_message_packet encode(const midi_message &msg)
{
    return midi_message_packet::fromMessage(msg);
}

int16_t decodePitchBend(const midi_message_packet &pkt)
{
    int pitchBendValue = (pkt.data2 << 7) | pkt.data1;
    return static_cast<int16_t>(pitchBendValue - 8192);
}

} // namespace

TEST_CASE("note on encodes status and channel", "[midi][packet]")
{
    SECTION("channel 1")
    {
        auto pkt = encode({1, MIDI_NOTE_ON, 60, 127});
        REQUIRE(pkt.statusByte == 0x90);
        REQUIRE(pkt.data1 == 60);
        REQUIRE(pkt.data2 == 127);
        REQUIRE(pkt.getDataSize() == 3);
    }

    SECTION("channel 16")
    {
        auto pkt = encode({16, MIDI_NOTE_ON, 60, 127});
        REQUIRE(pkt.statusByte == 0x9F);
        REQUIRE(pkt.getDataSize() == 3);
    }
}

TEST_CASE("channel voice messages round-trip", "[midi][packet]")
{
    midi_message original{3, MIDI_CONTROL_CHANGE, 7, 100};
    auto round = encode(original).toMessage();

    const uint8_t channel = round.channel;
    const MidiStatus status = round.status;
    const uint8_t firstByte = round.firstByte;
    const uint8_t secondByte = round.secondByte;

    REQUIRE(channel == 3);
    REQUIRE(status == MIDI_CONTROL_CHANGE);
    REQUIRE(firstByte == 7);
    REQUIRE(secondByte == 100);
}

TEST_CASE("two-byte channel messages", "[midi][packet]")
{
    SECTION("program change")
    {
        auto pkt = encode({5, MIDI_PROGRAM_CHANGE, 42, 0});
        REQUIRE(pkt.statusByte == 0xC4);
        REQUIRE(pkt.data1 == 42);
        REQUIRE(pkt.getDataSize() == 2);
    }

    SECTION("channel aftertouch")
    {
        auto pkt = encode({2, MIDI_AFTERTOUCH, 64, 0});
        REQUIRE(pkt.statusByte == 0xD1);
        REQUIRE(pkt.data1 == 64);
        REQUIRE(pkt.getDataSize() == 2);
    }
}

TEST_CASE("three-byte channel messages", "[midi][packet]")
{
    auto pkt = encode({1, MIDI_POLY_AFTERTOUCH, 60, 90});
    REQUIRE(pkt.statusByte == 0xA0);
    REQUIRE(pkt.data1 == 60);
    REQUIRE(pkt.data2 == 90);
    REQUIRE(pkt.getDataSize() == 3);
}

TEST_CASE("system messages ignore channel", "[midi][packet]")
{
    auto pkt = encode({99, MIDI_START, 0, 0});
    REQUIRE(pkt.statusByte == 0xFA);
    REQUIRE(pkt.getDataSize() == 1);

    auto round = pkt.toMessage();
    const uint8_t channel = round.channel;
    const MidiStatus status = round.status;

    REQUIRE(channel == 0);
    REQUIRE(status == MIDI_START);
}

TEST_CASE("system message sizes", "[midi][packet]")
{
    SECTION("clock is one byte")
    {
        auto pkt = encode({0, MIDI_TIME_CLOCK, 0, 0});
        REQUIRE(pkt.statusByte == 0xF8);
        REQUIRE(pkt.getDataSize() == 1);
    }

    SECTION("song select is two bytes")
    {
        auto pkt = encode({0, MIDI_SONG_SELECT, 12, 0});
        REQUIRE(pkt.statusByte == 0xF3);
        REQUIRE(pkt.data1 == 12);
        REQUIRE(pkt.getDataSize() == 2);
    }

    SECTION("song position is three bytes")
    {
        auto pkt = encode({0, MIDI_SONG_POS_POINTER, 0x34, 0x12});
        REQUIRE(pkt.statusByte == 0xF2);
        REQUIRE(pkt.getDataSize() == 3);
    }
}

TEST_CASE("pitch bend round trip", "[midi][packet]")
{
    SECTION("signed API: center is 0")
    {
        for (int16_t signedValue : {int16_t{-8192}, int16_t{0}, int16_t{8191}})
        {
            uint16_t raw = static_cast<uint16_t>(signedValue + 8192);
            auto pkt = encode({
                1,
                MIDI_PITCH_BEND,
                static_cast<uint8_t>(raw & 0x7F),
                static_cast<uint8_t>((raw >> 7) & 0x7F),
            });

            REQUIRE(decodePitchBend(pkt) == signedValue);
            REQUIRE(pkt.getDataSize() == 3);
        }
    }

    SECTION("raw wire: center is 8192")
    {
        for (uint16_t raw : {uint16_t{0}, uint16_t{8192}, uint16_t{16383}})
        {
            auto pkt = encode({
                1,
                MIDI_PITCH_BEND,
                static_cast<uint8_t>(raw & 0x7F),
                static_cast<uint8_t>((raw >> 7) & 0x7F),
            });

            REQUIRE(decodePitchBend(pkt) == static_cast<int16_t>(raw - 8192));
            REQUIRE(pkt.getDataSize() == 3);
        }
    }
}
