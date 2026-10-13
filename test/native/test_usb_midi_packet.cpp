#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include "include/UsbMidiPacket.h"

using Packet = std::array<uint8_t, 4>;

namespace {

Packet packetFor(MidiStatus status, uint8_t channel, uint8_t d1, uint8_t d2)
{
    midi_message msg;
    msg.channel = channel;
    msg.status = status;
    msg.firstByte = d1;
    msg.secondByte = d2;
    Packet p{0xEE, 0xEE, 0xEE, 0xEE};
    REQUIRE(enomik::toUsbMidiPacket(msg, p.data()));
    return p;
}

std::vector<Packet> sysexPackets(const std::vector<uint8_t> &data, int acceptPackets = 1000)
{
    std::vector<Packet> out;
    enomik::writeUsbMidiSysEx(data.data(), static_cast<uint16_t>(data.size()),
                              [&](const uint8_t p[4])
                              {
                                  if (static_cast<int>(out.size()) >= acceptPackets)
                                      return false;
                                  out.push_back({p[0], p[1], p[2], p[3]});
                                  return true;
                              });
    return out;
}

} // namespace

TEST_CASE("USB-MIDI packets for channel messages", "[usb][packet]")
{
    REQUIRE(packetFor(MIDI_NOTE_ON, 1, 60, 100) == Packet{0x09, 0x90, 60, 100});
    REQUIRE(packetFor(MIDI_NOTE_OFF, 16, 60, 0) == Packet{0x08, 0x8F, 60, 0});
    REQUIRE(packetFor(MIDI_CONTROL_CHANGE, 2, 7, 127) == Packet{0x0B, 0xB1, 7, 127});
    REQUIRE(packetFor(MIDI_POLY_AFTERTOUCH, 3, 60, 50) == Packet{0x0A, 0xA2, 60, 50});
    REQUIRE(packetFor(MIDI_PITCH_BEND, 1, 0x00, 0x40) == Packet{0x0E, 0xE0, 0x00, 0x40});
    REQUIRE(packetFor(MIDI_PROGRAM_CHANGE, 1, 5, 99) == Packet{0x0C, 0xC0, 5, 0});
    REQUIRE(packetFor(MIDI_AFTERTOUCH, 1, 70, 99) == Packet{0x0D, 0xD0, 70, 0});
    REQUIRE(packetFor(MIDI_NOTE_ON, 1, 0xFF, 0x80) == Packet{0x09, 0x90, 0x7F, 0x00}); // data masked
}

TEST_CASE("USB-MIDI packets for system messages", "[usb][packet]")
{
    REQUIRE(packetFor(MIDI_SONG_POS_POINTER, 0, 0x2C, 0x02) == Packet{0x03, 0xF2, 0x2C, 0x02});
    REQUIRE(packetFor(MIDI_SONG_SELECT, 0, 3, 9) == Packet{0x02, 0xF3, 3, 0});
    REQUIRE(packetFor(MIDI_TIME_CLOCK, 0, 0, 0) == Packet{0x0F, 0xF8, 0, 0});
    REQUIRE(packetFor(MIDI_START, 0, 0, 0) == Packet{0x0F, 0xFA, 0, 0});
    REQUIRE(packetFor(MIDI_CONTINUE, 0, 0, 0) == Packet{0x0F, 0xFB, 0, 0});
    REQUIRE(packetFor(MIDI_STOP, 0, 0, 0) == Packet{0x0F, 0xFC, 0, 0});
}

TEST_CASE("USB-MIDI packet builder rejects what it cannot send", "[usb][packet]")
{
    midi_message msg{};
    uint8_t p[4];
    msg.status = MIDI_NOTE_ON;
    msg.channel = 0;
    REQUIRE_FALSE(enomik::toUsbMidiPacket(msg, p));
    msg.channel = 17;
    REQUIRE_FALSE(enomik::toUsbMidiPacket(msg, p));
    msg.channel = 1;
    msg.status = MIDI_SYSEX;
    REQUIRE_FALSE(enomik::toUsbMidiPacket(msg, p));
    msg.status = MIDI_TIME_CODE;
    REQUIRE_FALSE(enomik::toUsbMidiPacket(msg, p));
}

TEST_CASE("SysEx is split into USB-MIDI packets", "[usb][sysex]")
{
    // Every possible last packet: 1, 2 and 3 bytes.
    REQUIRE(sysexPackets({0xF0, 0x7D, 0x01, 0xF7}) ==
            std::vector<Packet>{{0x04, 0xF0, 0x7D, 0x01}, {0x05, 0xF7, 0, 0}});
    REQUIRE(sysexPackets({0xF0, 0x7D, 0x01, 0x02, 0xF7}) ==
            std::vector<Packet>{{0x04, 0xF0, 0x7D, 0x01}, {0x06, 0x02, 0xF7, 0}});
    REQUIRE(sysexPackets({0xF0, 0x7D, 0x01, 0x02, 0x03, 0xF7}) ==
            std::vector<Packet>{{0x04, 0xF0, 0x7D, 0x01}, {0x07, 0x02, 0x03, 0xF7}});
    REQUIRE(sysexPackets({0xF0, 0xF7}) == std::vector<Packet>{{0x06, 0xF0, 0xF7, 0}});
    REQUIRE(sysexPackets({0xF0, 0x01, 0xF7}) == std::vector<Packet>{{0x07, 0xF0, 0x01, 0xF7}});
}

TEST_CASE("SysEx writing stops when USB is busy and rejects incomplete buffers", "[usb][sysex]")
{
    std::vector<uint8_t> big(100, 0x11);
    big.front() = 0xF0;
    big.back() = 0xF7;
    REQUIRE(sysexPackets(big).size() == 34);
    REQUIRE(sysexPackets(big, 5).size() == 5);
    REQUIRE_FALSE(enomik::writeUsbMidiSysEx(big.data(), 100, [](const uint8_t *) { return false; }));

    REQUIRE(sysexPackets({0x7D, 0x01, 0x02}).empty()); // no F0 / F7
    REQUIRE(sysexPackets({0xF0, 0x01}).empty());       // no F7
    REQUIRE(sysexPackets({}).empty());
}

namespace {

// USB buffer that holds `capacity` packets; the host reads `readPerMs` per ms of sleep.
struct FakeUsb
{
    int capacity = 16;
    int readPerMs = 4;
    int queued = 0;
    bool mounted = true;
    bool hostReads = true;
    uint32_t nowMs = 0;
    std::vector<Packet> written;

    bool write(const uint8_t p[4])
    {
        if (queued >= capacity)
            return false;
        queued++;
        written.push_back({p[0], p[1], p[2], p[3]});
        return true;
    }

    void sleep()
    {
        nowMs++;
        if (hostReads)
            queued = queued > readPerMs ? queued - readPerMs : 0;
    }

    bool send(enomik::UsbSysExPacer &pacer, const std::vector<uint8_t> &sysex)
    {
        return pacer.write(
            sysex.data(), static_cast<uint16_t>(sysex.size()),
            [this](const uint8_t p[4]) { return write(p); },
            [this]() { return mounted; },
            [this]() { return nowMs; },
            [this]() { sleep(); });
    }
};

std::vector<uint8_t> sysexOf(size_t length)
{
    std::vector<uint8_t> data(length, 0x22);
    data.front() = 0xF0;
    data.back() = 0xF7;
    return data;
}

} // namespace

TEST_CASE("SysEx larger than the USB buffer is written whole while the host reads", "[usb][sysex]")
{
    FakeUsb usb;
    enomik::UsbSysExPacer pacer;
    const auto sysex = sysexOf(150); // 50 packets, buffer holds 16
    REQUIRE(usb.send(pacer, sysex));
    REQUIRE(usb.written == sysexPackets(sysex));
}

TEST_CASE("a burst of SysEx replies all arrive whole", "[usb][sysex]")
{
    // Like a GET_CONFIG reply: about a dozen SysEx back to back
    FakeUsb usb;
    enomik::UsbSysExPacer pacer;
    for (int i = 0; i < 12; i++)
        REQUIRE(usb.send(pacer, sysexOf(14)));
    REQUIRE(usb.written.size() == 12 * 5);
}

TEST_CASE("a SysEx cut off because the host stopped reading still ends with F7", "[usb][sysex]")
{
    FakeUsb usb;
    usb.hostReads = false;
    enomik::UsbSysExPacer pacer;
    const auto sysex = sysexOf(150);

    // The end packet needs a free slot: the host reads one slot while ending
    REQUIRE_FALSE(enomik::writeUsbMidiSysExWaiting(
        sysex.data(), static_cast<uint16_t>(sysex.size()),
        [&](const uint8_t p[4]) { return usb.write(p); },
        [&](bool ending) {
            if (!ending)
                return false;
            usb.queued--;
            return true;
        }));
    REQUIRE(usb.written.size() == 17);
    REQUIRE(usb.written.back() == Packet{0x05, 0xF7, 0x00, 0x00});
}

TEST_CASE("pacer gives up after its budget and then fails fast", "[usb][sysex]")
{
    FakeUsb usb;
    usb.hostReads = false;
    enomik::UsbSysExPacer pacer;

    REQUIRE_FALSE(usb.send(pacer, sysexOf(150)));
    REQUIRE(usb.nowMs == enomik::UsbSysExPacer::BUDGET_MS + enomik::UsbSysExPacer::END_EXTRA_MS);

    // Within the back-off: no waiting at all
    const uint32_t before = usb.nowMs;
    REQUIRE_FALSE(usb.send(pacer, sysexOf(14)));
    REQUIRE(usb.nowMs == before);

    // After the back-off, with the host reading again, writes succeed
    usb.hostReads = true;
    usb.queued = 0;
    usb.nowMs += enomik::UsbSysExPacer::BACKOFF_MS;
    REQUIRE(usb.send(pacer, sysexOf(150)));
}

TEST_CASE("pacer does not wait when USB is gone", "[usb][sysex]")
{
    FakeUsb usb;
    usb.capacity = 0;
    usb.mounted = false;
    enomik::UsbSysExPacer pacer;
    REQUIRE_FALSE(usb.send(pacer, sysexOf(14)));
    REQUIRE(usb.nowMs == 0);
    REQUIRE(usb.written.empty());
}
