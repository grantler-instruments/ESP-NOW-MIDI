#include <catch2/catch_test_macros.hpp>

#include "MidiJitterBuffer.h"
#include "midiTimedPacket.h"

namespace {

midi_message_packet makeNoteOn(uint8_t channelZeroBased, uint8_t note, uint8_t vel)
{
  midi_message_packet pkt{};
  pkt.statusByte = static_cast<uint8_t>(MIDI_NOTE_ON | (channelZeroBased & 0x0F));
  pkt.data1 = note;
  pkt.data2 = vel;
  return pkt;
}

} // namespace

TEST_CASE("timed packet pack/parse round-trip", "[midi][timed]")
{
  uint8_t buf[8]{};
  auto midi = makeNoteOn(0, 60, 100);
  const uint16_t tick = 0x1234;

  const size_t n = midi_timed_packet::pack(buf, sizeof(buf), tick, midi);
  REQUIRE(n == 6);
  REQUIRE(buf[0] == ESP_NOW_MIDI_TIMED_MAGIC);
  REQUIRE(buf[1] == 0x34);
  REQUIRE(buf[2] == 0x12);

  uint16_t outTick = 0;
  midi_message_packet out{};
  REQUIRE(midi_timed_packet::parse(buf, static_cast<int>(n), outTick, out));
  REQUIRE(outTick == tick);
  REQUIRE(out.statusByte == midi.statusByte);
  REQUIRE(out.data1 == 60);
  REQUIRE(out.data2 == 100);
}

TEST_CASE("timed packet sizes follow MIDI data size", "[midi][timed]")
{
  uint8_t buf[8]{};

  SECTION("clock is 4 bytes")
  {
    midi_message_packet clock{};
    clock.statusByte = MIDI_TIME_CLOCK;
    REQUIRE(midi_timed_packet::pack(buf, sizeof(buf), 1, clock) == 4);
  }

  SECTION("program change is 5 bytes")
  {
    midi_message_packet pc{};
    pc.statusByte = 0xC0;
    pc.data1 = 7;
    REQUIRE(midi_timed_packet::pack(buf, sizeof(buf), 2, pc) == 5);
  }
}

TEST_CASE("timed frame detection and rejection", "[midi][timed]")
{
  uint8_t rawNote[] = {0x90, 60, 127};
  REQUIRE_FALSE(midi_timed_packet::isTimedFrame(rawNote, 3));

  uint8_t badStatus[] = {ESP_NOW_MIDI_TIMED_MAGIC, 0, 0, 0x40, 60, 127};
  uint16_t tick = 0;
  midi_message_packet midi{};
  REQUIRE(midi_timed_packet::isTimedFrame(badStatus, 6));
  REQUIRE_FALSE(midi_timed_packet::parse(badStatus, 6, tick, midi));

  uint8_t wrongLen[] = {ESP_NOW_MIDI_TIMED_MAGIC, 0, 0, 0x90, 60}; // note-on needs 3 midi bytes
  REQUIRE_FALSE(midi_timed_packet::parse(wrongLen, 5, tick, midi));
}

TEST_CASE("realtime helper", "[midi][timed]")
{
  REQUIRE(isMidiRealtimeStatus(MIDI_TIME_CLOCK));
  REQUIRE(isMidiRealtimeStatus(MIDI_START));
  REQUIRE(isMidiRealtimeStatus(MIDI_CONTINUE));
  REQUIRE(isMidiRealtimeStatus(MIDI_STOP));
  REQUIRE_FALSE(isMidiRealtimeStatus(MIDI_NOTE_ON));
  REQUIRE_FALSE(isMidiRealtimeStatus(MIDI_ACTIVE_SENSING));
}

TEST_CASE("jitter buffer first packet anchors to now+T", "[midi][jitter]")
{
  MidiJitterBuffer buf;
  uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  auto *peer = buf.findOrAllocPeer(MidiJitterBuffer::packMac(mac));
  REQUIRE(peer != nullptr);

  midi_message msg{1, MIDI_NOTE_ON, 60, 100};
  MidiJitterBuffer::Entry forced{};
  bool hasForced = false;
  const uint32_t now = 1'000'000;
  const uint32_t T = 8'000;

  buf.push(*peer, now, /*tick*/ 10, msg, T, 3'000'000, 100'000, forced, hasForced);
  REQUIRE_FALSE(hasForced);
  REQUIRE(peer->count == 1);

  MidiJitterBuffer::Entry front{};
  REQUIRE(MidiJitterBuffer::peek(*peer, front));
  REQUIRE(front.playout_us == now + T);

  REQUIRE_FALSE(MidiJitterBuffer::popDue(*peer, now + T - 1, front));
  REQUIRE(MidiJitterBuffer::popDue(*peer, now + T, front));
  REQUIRE(front.message.firstByte == 60);
}

TEST_CASE("jitter buffer late packet plays ASAP", "[midi][jitter]")
{
  MidiJitterBuffer buf;
  auto *peer = buf.findOrAllocPeer(1);
  REQUIRE(peer != nullptr);

  midi_message msg{1, MIDI_NOTE_ON, 61, 100};
  MidiJitterBuffer::Entry forced{};
  bool hasForced = false;
  const uint32_t T = 8'000;

  buf.push(*peer, 1'000'000, 0, msg, T, 3'000'000, 100'000, forced, hasForced);

  // Jump far ahead so the next tick is late relative to the session timeline.
  msg.firstByte = 62;
  buf.push(*peer, 1'000'000 + 50'000, /*tick still 0*/ 0, msg, T, 3'000'000, 100'000, forced,
           hasForced);

  MidiJitterBuffer::Entry a{}, b{};
  REQUIRE(MidiJitterBuffer::popDue(*peer, 1'000'000 + 50'000, a));
  REQUIRE(a.message.firstByte == 61);
  REQUIRE(MidiJitterBuffer::popDue(*peer, 1'000'000 + 50'000, b));
  REQUIRE(b.message.firstByte == 62);
  REQUIRE(b.playout_us == 1'000'000 + 50'000);
}

TEST_CASE("jitter buffer full force-releases oldest", "[midi][jitter]")
{
  MidiJitterBuffer buf;
  auto *peer = buf.findOrAllocPeer(2);
  REQUIRE(peer != nullptr);

  MidiJitterBuffer::Entry forced{};
  bool hasForced = false;
  const uint32_t T = 8'000;
  uint32_t now = 0;

  for (int i = 0; i < ESP_NOW_MIDI_JITTER_BUFFER_SIZE; ++i)
  {
    midi_message msg{1, MIDI_NOTE_ON, static_cast<uint8_t>(i), 1};
    buf.push(*peer, now, static_cast<uint16_t>(i), msg, T, 3'000'000, 100'000, forced, hasForced);
    REQUIRE_FALSE(hasForced);
    now += 100; // 1 tick
  }
  REQUIRE(peer->count == ESP_NOW_MIDI_JITTER_BUFFER_SIZE);

  midi_message msg{1, MIDI_NOTE_ON, 99, 1};
  buf.push(*peer, now, static_cast<uint16_t>(ESP_NOW_MIDI_JITTER_BUFFER_SIZE), msg, T, 3'000'000,
           100'000, forced, hasForced);
  REQUIRE(hasForced);
  REQUIRE(forced.message.firstByte == 0);
  REQUIRE(peer->count == ESP_NOW_MIDI_JITTER_BUFFER_SIZE);
}

TEST_CASE("jitter buffer reanchors after gap", "[midi][jitter]")
{
  MidiJitterBuffer buf;
  auto *peer = buf.findOrAllocPeer(3);
  REQUIRE(peer != nullptr);

  MidiJitterBuffer::Entry forced{};
  bool hasForced = false;
  const uint32_t T = 8'000;

  midi_message msg{1, MIDI_NOTE_ON, 10, 1};
  buf.push(*peer, 0, 0, msg, T, 3'000'000, 100'000, forced, hasForced);

  // Drain first note so only session state remains.
  MidiJitterBuffer::Entry out{};
  REQUIRE(MidiJitterBuffer::popDue(*peer, T, out));

  msg.firstByte = 11;
  const uint32_t later = 3'000'001;
  buf.push(*peer, later, /*old tick timeline*/ 5, msg, T, 3'000'000, 100'000, forced, hasForced);

  REQUIRE(MidiJitterBuffer::peek(*peer, out));
  REQUIRE(out.playout_us == later + T);
}
