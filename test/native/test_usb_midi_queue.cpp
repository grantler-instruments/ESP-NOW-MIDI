#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "include/UsbMidiQueue.h"

namespace {

midi_message msg(MidiStatus status, uint8_t channel, uint8_t a = 0, uint8_t b = 0)
{
    midi_message m;
    m.status = status;
    m.channel = channel;
    m.firstByte = a;
    m.secondByte = b;
    return m;
}

midi_message noteOn(uint8_t note, uint8_t vel = 100, uint8_t ch = 1) { return msg(MIDI_NOTE_ON, ch, note, vel); }
midi_message noteOff(uint8_t note, uint8_t ch = 1) { return msg(MIDI_NOTE_OFF, ch, note, 0); }
midi_message cc(uint8_t num, uint8_t val, uint8_t ch = 1) { return msg(MIDI_CONTROL_CHANGE, ch, num, val); }

std::vector<midi_message> drain(enomik::UsbMidiQueue &q)
{
    std::vector<midi_message> out;
    midi_message m;
    while (q.peek(m))
    {
        out.push_back(m);
        q.consumeHead();
    }
    return out;
}

bool same(const midi_message &a, const midi_message &b)
{
    return a.status == b.status && a.channel == b.channel && a.firstByte == b.firstByte &&
           a.secondByte == b.secondByte;
}

} // namespace

TEST_CASE("UsbMidiQueue delivers in order without stalls", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(noteOn(60), 0);
    q.enqueue(cc(1, 10), 1);
    q.enqueue(noteOff(60), 2);
    REQUIRE(q.pendingCount() == 3);
    REQUIRE(q.dropStale(100, 500) == 0);

    auto out = drain(q);
    REQUIRE(out.size() == 3);
    REQUIRE(same(out[0], noteOn(60)));
    REQUIRE(same(out[1], cc(1, 10)));
    REQUIRE(same(out[2], noteOff(60)));
    REQUIRE_FALSE(q.hasPending());
}

TEST_CASE("UsbMidiQueue fresh messages are never dropped", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(noteOn(60), 1000);
    REQUIRE(q.dropStale(1500, 500) == 0); // exactly at the limit: still fresh
    REQUIRE(q.pendingCount() == 1);
    REQUIRE(q.dropStale(1501, 500) == 1);
    REQUIRE(q.pendingCount() == 0);
    REQUIRE(q.staleDropCount() == 1);
}

TEST_CASE("UsbMidiQueue stale timeout 0 disables dropping", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(noteOn(60), 0);
    q.enqueueClock(0);
    REQUIRE(q.dropStale(100000, 0) == 0);
    REQUIRE(q.pendingCount() == 2);
}

TEST_CASE("UsbMidiQueue keeps release messages when stale", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(noteOn(60), 0);
    q.enqueue(noteOff(60), 0);
    q.enqueue(noteOn(62, 0), 0);                    // note on vel 0 = note off
    q.enqueue(cc(64, 0), 0);                        // sustain off
    q.enqueue(cc(123, 0), 0);                       // all notes off
    q.enqueue(msg(MIDI_STOP, 0), 0);
    q.enqueue(msg(MIDI_START, 0), 0);
    q.enqueue(msg(MIDI_POLY_AFTERTOUCH, 1, 60, 50), 0);

    REQUIRE(q.dropStale(1000, 500) == 3); // note on, start, poly pressure

    auto out = drain(q);
    REQUIRE(out.size() == 5);
    REQUIRE(same(out[0], noteOff(60)));
    REQUIRE(same(out[1], noteOn(62, 0)));
    REQUIRE(same(out[2], cc(64, 0)));
    REQUIRE(same(out[3], cc(123, 0)));
    REQUIRE(static_cast<int>(out[4].status) == MIDI_STOP);
}

TEST_CASE("UsbMidiQueue keeps the latest value of each state", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    for (uint8_t v = 0; v < 10; ++v)
    {
        q.enqueue(cc(7, v, 1), 0);  // volume ch1
        q.enqueue(cc(7, v, 2), 0);  // volume ch2: separate state
        q.enqueue(cc(10, v, 1), 0); // pan ch1: separate state
    }
    q.enqueue(msg(MIDI_PITCH_BEND, 1, 0x00, 0x30), 0);
    q.enqueue(msg(MIDI_PITCH_BEND, 1, 0x00, 0x40), 0);
    q.enqueue(msg(MIDI_PROGRAM_CHANGE, 1, 5), 0);
    q.enqueue(msg(MIDI_AFTERTOUCH, 1, 20), 0);
    q.enqueue(msg(MIDI_AFTERTOUCH, 1, 0), 0);

    q.dropStale(1000, 500);

    auto out = drain(q);
    REQUIRE(out.size() == 6);
    REQUIRE(same(out[0], cc(7, 9, 1)));
    REQUIRE(same(out[1], cc(7, 9, 2)));
    REQUIRE(same(out[2], cc(10, 9, 1)));
    REQUIRE(same(out[3], msg(MIDI_PITCH_BEND, 1, 0x00, 0x40)));
    REQUIRE(same(out[4], msg(MIDI_PROGRAM_CHANGE, 1, 5)));
    REQUIRE(same(out[5], msg(MIDI_AFTERTOUCH, 1, 0)));
}

TEST_CASE("UsbMidiQueue stale state superseded by a fresh value is dropped", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(cc(1, 10), 0);
    q.enqueue(noteOn(60), 0);
    q.enqueue(cc(1, 99), 900); // fresh
    q.enqueue(noteOn(61), 900); // fresh

    REQUIRE(q.dropStale(1000, 500) == 2);
    auto out = drain(q);
    REQUIRE(out.size() == 2);
    REQUIRE(same(out[0], cc(1, 99)));
    REQUIRE(same(out[1], noteOn(61)));
}

TEST_CASE("UsbMidiQueue drops a stale coalesced clock", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueueClock(0);
    REQUIRE(q.dropStale(400, 500) == 0);
    REQUIRE(q.hasPending());
    q.enqueueClock(400); // refreshed by a newer tick
    REQUIRE(q.dropStale(800, 500) == 0);
    REQUIRE(q.dropStale(1000, 500) == 1);
    REQUIRE_FALSE(q.hasPending());
}

TEST_CASE("UsbMidiQueue handles wrap-around", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    // Move head/tail around the ring a few times.
    for (int round = 0; round < 3; ++round)
    {
        for (uint8_t i = 0; i < 40; ++i)
        {
            q.enqueue(noteOn(i), 0);
        }
        drain(q);
    }
    for (uint8_t i = 0; i < 40; ++i)
    {
        q.enqueue(i % 2 ? noteOff(i) : noteOn(i), 0);
    }
    REQUIRE(q.dropStale(1000, 500) == 20);
    auto out = drain(q);
    REQUIRE(out.size() == 20);
    for (size_t k = 0; k < out.size(); ++k)
    {
        REQUIRE(same(out[k], noteOff(static_cast<uint8_t>(2 * k + 1))));
    }
}

TEST_CASE("UsbMidiQueue overflow evicts oldest droppable, keeps releases", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    const uint16_t capacity = USB_MIDI_QUEUE_SIZE - 1;

    q.enqueue(noteOff(1), 0); // oldest, a release
    for (uint16_t i = 1; i < capacity; ++i)
    {
        q.enqueue(noteOn(static_cast<uint8_t>(i % 128)), 0);
    }
    REQUIRE(q.pendingCount() == capacity);

    q.enqueue(noteOn(127, 1), 1); // newest must get in
    REQUIRE(q.pendingCount() == capacity);
    REQUIRE(q.overflowDropCount() == 1);

    auto out = drain(q);
    REQUIRE(out.size() == capacity);
    REQUIRE(same(out.front(), noteOff(1)));          // release survived
    REQUIRE(same(out[1], noteOn(2)));                // noteOn(1) was evicted
    REQUIRE(same(out.back(), noteOn(127, 1)));       // newest delivered
}

TEST_CASE("UsbMidiQueue overflow full of releases", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    const uint16_t capacity = USB_MIDI_QUEUE_SIZE - 1;
    for (uint16_t i = 0; i < capacity; ++i)
    {
        q.enqueue(noteOff(static_cast<uint8_t>(i)), 0);
    }

    q.enqueue(noteOn(100), 0); // not a release: dropped
    REQUIRE(q.pendingCount() == capacity);
    q.enqueue(noteOff(100), 0); // release: oldest release makes room
    REQUIRE(q.pendingCount() == capacity);
    REQUIRE(q.overflowDropCount() == 2);

    auto out = drain(q);
    REQUIRE(same(out.front(), noteOff(1)));
    REQUIRE(same(out.back(), noteOff(100)));
}

TEST_CASE("UsbMidiQueue survives millis() rollover", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(noteOn(60), 0xFFFFFF00u);
    REQUIRE(q.dropStale(0x00000010u, 500) == 0); // 0x110 ms old
    REQUIRE(q.dropStale(0x00000200u, 500) == 1); // 0x300 ms old
}

TEST_CASE("UsbMidiQueue clear resets pending entries", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(noteOn(60), 0);
    q.enqueueClock(0);
    q.clear();
    REQUIRE_FALSE(q.hasPending());
    REQUIRE(q.pendingCount() == 0);
}

TEST_CASE("UsbMidiQueue refreshTimestamps makes waiting messages fresh", "[dongle][usbqueue]")
{
    enomik::UsbMidiQueue q;
    q.enqueue(noteOn(60), 0);
    q.enqueueClock(0);
    q.refreshTimestamps(5000);
    REQUIRE(q.dropStale(5400, 500) == 0);
    REQUIRE(q.pendingCount() == 2);
    REQUIRE(q.dropStale(5600, 500) == 2);
}
