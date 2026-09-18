#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

#include "include/esp_now_midi_sysex.h"
#include "include/esp_now_midi_helpers.h"
#include "include/enomik_sysex_codec.h"

using namespace esp_now_midi_sysex;

namespace {

const uint8_t MAC_A[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
const uint8_t MAC_B[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

std::vector<uint8_t> makePayload(uint16_t len, uint8_t seed = 0)
{
    std::vector<uint8_t> data(len);
    for (uint16_t i = 0; i < len; ++i)
        data[i] = static_cast<uint8_t>((seed + i) & 0xFF);
    return data;
}

bool sendAllFragments(Reassembler &reasm, const uint8_t mac[6], const std::vector<uint8_t> &msg,
                      uint32_t nowMs, const uint8_t *&outData, uint16_t &outLen)
{
    const uint8_t total = fragmentCount(static_cast<uint16_t>(msg.size()));
    outData = nullptr;
    outLen = 0;
    bool complete = false;
    for (uint8_t seq = 0; seq < total; ++seq)
    {
        uint8_t frame[MAX_FRAME];
        const uint16_t offset = fragmentPayloadOffset(seq);
        const uint16_t payloadLen = fragmentPayloadLength(static_cast<uint16_t>(msg.size()), seq);
        const size_t frameLen = encodeFrame(frame, sizeof(frame), seq, total,
                                            static_cast<uint16_t>(msg.size()),
                                            msg.data() + offset, payloadLen);
        REQUIRE(frameLen > 0);
        complete = reasm.feed(mac, frame, static_cast<int>(frameLen), nowMs, outData, outLen);
        if (seq + 1 < total)
            REQUIRE_FALSE(complete);
    }
    return complete;
}

} // namespace

TEST_CASE("sysex fragment helpers", "[sysex][frame]")
{
    REQUIRE(fragmentCount(1) == 1);
    REQUIRE(fragmentCount(240) == 1);
    REQUIRE(fragmentCount(241) == 2);
    REQUIRE(fragmentCount(1024) == 5);
    REQUIRE(fragmentPayloadLength(241, 0) == 240);
    REQUIRE(fragmentPayloadLength(241, 1) == 1);
}

TEST_CASE("sysex single-fragment round trip", "[sysex][frame]")
{
    auto msg = makePayload(1, 0x10);
    uint8_t frame[MAX_FRAME];
    const size_t frameLen = encodeFrame(frame, sizeof(frame), 0, 1, 1, msg.data(), 1);
    REQUIRE(frameLen == HEADER_SIZE + 1);

    FrameView view{};
    REQUIRE(parseFrame(frame, static_cast<int>(frameLen), view));
    REQUIRE(view.seq == 0);
    REQUIRE(view.total == 1);
    REQUIRE(view.msgLen == 1);
    REQUIRE(view.payloadLen == 1);
    REQUIRE(view.payload[0] == 0x10);
    REQUIRE((view.flags & FLAG_FIRST) != 0);
    REQUIRE((view.flags & FLAG_LAST) != 0);
}

TEST_CASE("sysex reassemble multi-fragment", "[sysex][reasm]")
{
    Reassembler reasm;
    auto msg = makePayload(241, 7);
    const uint8_t *out = nullptr;
    uint16_t outLen = 0;
    REQUIRE(sendAllFragments(reasm, MAC_A, msg, 1000, out, outLen));
    REQUIRE(outLen == 241);
    REQUIRE(std::memcmp(out, msg.data(), 241) == 0);
}

TEST_CASE("sysex reassemble max message", "[sysex][reasm]")
{
    Reassembler reasm;
    auto msg = makePayload(MAX_MESSAGE, 3);
    const uint8_t *out = nullptr;
    uint16_t outLen = 0;
    REQUIRE(sendAllFragments(reasm, MAC_A, msg, 1000, out, outLen));
    REQUIRE(outLen == MAX_MESSAGE);
    REQUIRE(std::memcmp(out, msg.data(), MAX_MESSAGE) == 0);
}

TEST_CASE("sysex rejects bad frames", "[sysex][frame]")
{
    auto msg = makePayload(10, 1);
    uint8_t frame[MAX_FRAME];
    const size_t frameLen = encodeFrame(frame, sizeof(frame), 0, 1, 10, msg.data(), 10);
    REQUIRE(frameLen > 0);

    FrameView view{};
    SECTION("bad marker")
    {
        frame[0] = 0x00;
        REQUIRE_FALSE(parseFrame(frame, static_cast<int>(frameLen), view));
    }
    SECTION("bad version")
    {
        frame[1] = (2 << VERSION_SHIFT) | FLAG_FIRST | FLAG_LAST;
        REQUIRE_FALSE(parseFrame(frame, static_cast<int>(frameLen), view));
    }
    SECTION("truncated")
    {
        REQUIRE_FALSE(parseFrame(frame, 5, view));
    }
    SECTION("oversized message length field")
    {
        frame[4] = 0x01;
        frame[5] = 0x04; // 1025
        REQUIRE_FALSE(parseFrame(frame, static_cast<int>(frameLen), view));
    }
}

TEST_CASE("sysex drops on sequence gap", "[sysex][reasm]")
{
    Reassembler reasm;
    auto msg = makePayload(241, 9);
    uint8_t frame0[MAX_FRAME];
    uint8_t frame1[MAX_FRAME];
    const size_t len0 = encodeFrame(frame0, sizeof(frame0), 0, 2, 241, msg.data(), 240);
    const size_t len1 = encodeFrame(frame1, sizeof(frame1), 1, 2, 241, msg.data() + 240, 1);
    REQUIRE(len0 > 0);
    REQUIRE(len1 > 0);

    const uint8_t *out = nullptr;
    uint16_t outLen = 0;
    REQUIRE_FALSE(reasm.feed(MAC_A, frame0, static_cast<int>(len0), 100, out, outLen));
    // Skip feeding seq 0 again; feed seq 1 without seq 0 first after clear via wrong order:
    // Feed seq1 while expecting seq1 would work - instead feed after resetting by feeding only seq1 to empty
    Reassembler reasm2;
    REQUIRE_FALSE(reasm2.feed(MAC_B, frame1, static_cast<int>(len1), 100, out, outLen));
}

TEST_CASE("sysex timeout clears incomplete assembly", "[sysex][reasm]")
{
    Reassembler reasm;
    auto msg = makePayload(241, 2);
    uint8_t frame0[MAX_FRAME];
    uint8_t frame1[MAX_FRAME];
    const size_t len0 = encodeFrame(frame0, sizeof(frame0), 0, 2, 241, msg.data(), 240);
    const size_t len1 = encodeFrame(frame1, sizeof(frame1), 1, 2, 241, msg.data() + 240, 1);
    REQUIRE(len0 > 0);
    REQUIRE(len1 > 0);

    const uint8_t *out = nullptr;
    uint16_t outLen = 0;
    REQUIRE_FALSE(reasm.feed(MAC_A, frame0, static_cast<int>(len0), 1000, out, outLen));
    // After timeout, late fragment should not complete.
    REQUIRE_FALSE(reasm.feed(MAC_A, frame1, static_cast<int>(len1),
                             1000 + REASSEMBLY_TIMEOUT_MS + 1, out, outLen));
}

TEST_CASE("sysex rejects empty and oversize encode", "[sysex][frame]")
{
    uint8_t frame[MAX_FRAME];
    uint8_t byte = 0xF0;
    REQUIRE(encodeFrame(frame, sizeof(frame), 0, 1, 0, &byte, 0) == 0);

    auto big = makePayload(MAX_MESSAGE + 1);
    // fragmentCount for 1025 would be 5, but encode should reject msgLen > MAX
    REQUIRE(encodeFrame(frame, sizeof(frame), 0, fragmentCount(MAX_MESSAGE + 1),
                        MAX_MESSAGE + 1, big.data(), MAX_PAYLOAD) == 0);
}

TEST_CASE("toMidiMessage clamps to midi_sysex_message capacity", "[sysex][codec]")
{
    enomik::SysExPacket pkt;
    pkt.length = midi_sysex_message::MAX_DATA_SIZE;
    pkt.data[0] = 0xF0;
    pkt.data[pkt.length - 1] = 0xF7;
    for (uint16_t i = 1; i + 1 < pkt.length; ++i)
        pkt.data[i] = 0x01;

    auto msg = enomik::SysExEncoder::toMidiMessage(pkt);
    REQUIRE(msg.length == midi_sysex_message::MAX_DATA_SIZE);
    REQUIRE(msg.data[0] == 0xF0);
    REQUIRE(msg.data[msg.length - 1] == 0xF7);
}
