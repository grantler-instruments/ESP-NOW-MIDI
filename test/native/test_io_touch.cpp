// enomik::IO touch inputs (baseline-relative, both chip polarities) and the
// SysEx raw stream used to tune them.

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <vector>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"

namespace {

constexpr uint8_t kPin = 4;
constexpr uint32_t kBaseline = 20000;

std::map<int, uint32_t> g_touch;
uint32_t readTouch(uint8_t pin) { return g_touch[pin]; }

// Raw reading for a touch of `percent` change from kBaseline.
uint32_t touchAt(bool rises, uint32_t percent)
{
    const uint32_t delta = kBaseline * percent / 100;
    return rises ? kBaseline + delta : kBaseline - delta;
}

struct Rig
{
    enomik::IO io;
    std::vector<midi_message> midi;
    std::vector<std::vector<uint8_t>> sysex;

    explicit Rig(bool rises)
    {
        io.begin();
        io.setTouchReader(readTouch, rises);
        io.setOnMIDISendRequest([this](midi_message m) { midi.push_back(m); });
        io.setOnSysExSendRequest([this](midi_sysex_message m) {
            sysex.emplace_back(m.data, m.data + m.length);
        });
        g_touch[kPin] = kBaseline;
    }

    void setPin(uint8_t threshold, uint8_t minValue = 0, uint8_t maxValue = 127)
    {
        enomik::PinConfig cfg(kPin, enomik::ENOMIK_INPUT_TOUCH);
        cfg.threshold = threshold;
        cfg.midi_type = MidiStatus::MIDI_CONTROL_CHANGE;
        cfg.midi_cc = 20;
        cfg.min_midi_value = minValue;
        cfg.max_midi_value = maxValue;
        send(enomik::SysExEncoder::encodePinConfig(cfg, enomik::SysExCommand::SET_PIN_CONFIG));
    }

    void send(const enomik::SysExPacket &pkt) { io.onSysEx(pkt.data, pkt.length); }

    // Runs loop() for `ms` in 5 ms steps.
    void run(int ms)
    {
        for (int t = 0; t < ms; t += 5)
        {
            millisNow() += 5;
            io.loop();
        }
    }

    int lastValue() const { return midi.empty() ? -1 : midi.back().secondByte; }

    std::vector<enomik::RawValue> frames() const
    {
        std::vector<enomik::RawValue> out;
        for (const auto &m : sysex)
        {
            if (m.size() < 6 || m[4] != static_cast<uint8_t>(enomik::SysExCommand::RAW_VALUE))
                continue;
            enomik::RawValue v;
            REQUIRE(enomik::SysExDecoder::decodeRawValue(m.data() + 5, m.size() - 6, v));
            out.push_back(v);
        }
        return out;
    }
};

enomik::SysExPacket streamRequest(uint8_t pin, bool enable, uint8_t interval = 0)
{
    enomik::RawStreamRequest req;
    req.pin = pin;
    req.enable = enable;
    req.interval = interval;
    return enomik::SysExEncoder::encodeRawStream(req, enomik::SysExCommand::SET_RAW_STREAM);
}

} // namespace

TEST_CASE("32-bit septets round-trip", "[sysex][raw]")
{
    for (uint32_t value : {0u, 1u, 127u, 128u, 20000u, (1u << 28) + 5, 0xFFFFFFFFu})
    {
        uint8_t buf[enomik::SEPTET32_SIZE];
        enomik::encodeSeptets32(value, buf);
        for (uint8_t b : buf)
            REQUIRE(b < 0x80);
        REQUIRE(enomik::decodeSeptets32(buf) == value);
    }
}

TEST_CASE("RAW_VALUE frame round-trips", "[sysex][raw]")
{
    enomik::RawValue v;
    v.pin = 12;
    v.mode = enomik::ENOMIK_INPUT_TOUCH;
    v.raw = 0xFFFFFFF0u;
    v.baseline = 180000;
    v.peak = 9000;
    v.percent = 127;
    v.norm = 64;
    v.threshold = 30;
    v.active = true;

    const auto pkt = enomik::SysExEncoder::encodeRawValue(v);
    REQUIRE(pkt.data[pkt.length - 1] == 0xF7);
    for (size_t i = 1; i + 1 < pkt.length; i++)
        REQUIRE(pkt.data[i] < 0x80);

    enomik::RawValue got;
    REQUIRE(enomik::SysExDecoder::decodeRawValue(pkt.data + 5, pkt.length - 6, got));
    REQUIRE(got.pin == v.pin);
    REQUIRE(got.mode == v.mode);
    REQUIRE(got.raw == v.raw);
    REQUIRE(got.baseline == v.baseline);
    REQUIRE(got.peak == v.peak);
    REQUIRE(got.percent == v.percent);
    REQUIRE(got.norm == v.norm);
    REQUIRE(got.threshold == v.threshold);
    REQUIRE(got.active);
}

TEST_CASE("touch threshold switches between min and max on both chip polarities", "[io][touch]")
{
    for (bool rises : {false, true})
    {
        INFO("rises " << rises);
        Rig rig(rises);
        rig.setPin(20, 10, 100);
        rig.run(50);
        REQUIRE(rig.midi.empty()); // untouched: nothing sent

        g_touch[kPin] = touchAt(rises, 25);
        rig.run(20);
        REQUIRE(rig.midi.size() == 1);
        REQUIRE(rig.lastValue() == 100);

        // Between release (15%) and trigger (20%): stays on.
        g_touch[kPin] = touchAt(rises, 17);
        rig.run(50);
        REQUIRE(rig.midi.size() == 1);

        g_touch[kPin] = touchAt(rises, 10);
        rig.run(20);
        REQUIRE(rig.midi.size() == 2);
        REQUIRE(rig.lastValue() == 10);

        // Change against the touch direction never triggers.
        g_touch[kPin] = touchAt(!rises, 50);
        rig.run(50);
        REQUIRE(rig.midi.size() == 2);
    }
}

TEST_CASE("touch continuous mode normalizes to the learned peak", "[io][touch]")
{
    for (bool rises : {false, true})
    {
        INFO("rises " << rises);
        Rig rig(rises);
        rig.setPin(0);
        rig.run(100);
        REQUIRE(rig.lastValue() == 0);

        // Idle noise (1%) stays near 0: peak floor is TOUCH_MIN_PEAK_PCT.
        g_touch[kPin] = touchAt(rises, 1);
        rig.run(200);
        REQUIRE(rig.lastValue() <= 30);

        // Firm touch learns the peak.
        g_touch[kPin] = touchAt(rises, 40);
        rig.run(500);
        REQUIRE(rig.lastValue() == 127);

        // Half of it reads about half.
        g_touch[kPin] = touchAt(rises, 20);
        rig.run(500);
        REQUIRE(rig.lastValue() >= 60);
        REQUIRE(rig.lastValue() <= 67);

        g_touch[kPin] = kBaseline;
        rig.run(500);
        REQUIRE(rig.lastValue() == 0);
    }
}

TEST_CASE("touch continuous mode maps onto an inverted min/max", "[io][touch]")
{
    Rig rig(true);
    rig.setPin(0, 100, 20);
    rig.run(100);
    REQUIRE(rig.lastValue() == 100);
    g_touch[kPin] = touchAt(true, 40);
    rig.run(500);
    REQUIRE(rig.lastValue() == 20);
}

TEST_CASE("touch baseline follows slow drift while untouched", "[io][touch]")
{
    Rig rig(true);
    rig.setPin(20);
    // 8% drift (below threshold/2 = 10%): baseline follows, no trigger.
    g_touch[kPin] = touchAt(true, 8);
    rig.run(5000);
    REQUIRE(rig.midi.empty());

    enomik::RawValue v;
    REQUIRE(rig.io.readPinRaw(kPin, v));
    REQUIRE(v.baseline > kBaseline + kBaseline * 7 / 100);
    REQUIRE(v.percent <= 1);
}

TEST_CASE("threshold-only change keeps the touch calibration", "[io][touch]")
{
    Rig rig(true);
    rig.setPin(20);
    rig.run(20);

    // Finger on the pad while the user tunes the threshold.
    g_touch[kPin] = touchAt(true, 50);
    rig.run(20);
    rig.setPin(30);
    rig.run(20);

    enomik::RawValue v;
    REQUIRE(rig.io.readPinRaw(kPin, v));
    REQUIRE(v.baseline == kBaseline);
    REQUIRE(v.threshold == 30);
    REQUIRE(v.active);

    // Any other change recalibrates.
    rig.setPin(30, 0, 100);
    REQUIRE(rig.io.readPinRaw(kPin, v));
    REQUIRE(v.baseline == touchAt(true, 50));
}

TEST_CASE("raw stream sends frames until switched off", "[io][raw]")
{
    Rig rig(true);
    rig.setPin(20);
    rig.sysex.clear();

    rig.send(streamRequest(kPin, true, 10)); // 50 ms
    REQUIRE(rig.io.isRawStreaming());
    REQUIRE(rig.sysex.size() == 1);
    const auto &ack = rig.sysex[0];
    REQUIRE(ack[4] == static_cast<uint8_t>(enomik::SysExCommand::SET_RAW_STREAM_RESPONSE));
    REQUIRE(ack[5] == kPin);
    REQUIRE(ack[6] == 1);
    REQUIRE(ack[7] == 10);

    g_touch[kPin] = touchAt(true, 30);
    rig.run(500);
    auto frames = rig.frames();
    REQUIRE(frames.size() >= 9);
    REQUIRE(frames.size() <= 11);
    const auto &last = frames.back();
    REQUIRE(last.pin == kPin);
    REQUIRE(last.mode == enomik::ENOMIK_INPUT_TOUCH);
    REQUIRE(last.raw == touchAt(true, 30));
    REQUIRE(last.baseline == kBaseline);
    REQUIRE(last.percent == 30);
    REQUIRE(last.threshold == 20);
    REQUIRE(last.active);

    // Threshold tuned while streaming shows up in frames.
    rig.setPin(50); // releases below 37%
    rig.run(100);
    REQUIRE(rig.frames().back().threshold == 50);
    REQUIRE_FALSE(rig.frames().back().active);

    rig.send(streamRequest(kPin, false));
    REQUIRE_FALSE(rig.io.isRawStreaming());
    rig.sysex.clear();
    rig.run(500);
    REQUIRE(rig.frames().empty());
}

TEST_CASE("raw stream clamps the interval and covers all input pins", "[io][raw]")
{
    Rig rig(true);
    rig.setPin(0);
    enomik::PinConfig analog(7, enomik::ENOMIK_ANALOG_INPUT);
    rig.send(enomik::SysExEncoder::encodePinConfig(analog, enomik::SysExCommand::SET_PIN_CONFIG));
    enomik::PinConfig out(8, enomik::ENOMIK_OUTPUT);
    rig.send(enomik::SysExEncoder::encodePinConfig(out, enomik::SysExCommand::SET_PIN_CONFIG));
    rig.sysex.clear();

    rig.send(streamRequest(enomik::RAW_STREAM_ALL_PINS, true, 1)); // 5 ms, clamped to 20
    REQUIRE(rig.sysex[0][7] == enomik::IO::RAW_STREAM_MIN_MS / 5);

    rig.run(200);
    auto frames = rig.frames();
    REQUIRE(frames.size() >= 18); // ~10 rounds x 2 input pins, output pin skipped
    REQUIRE(frames.size() <= 22);
    for (const auto &f : frames)
        REQUIRE((f.pin == kPin || f.pin == 7));
}

TEST_CASE("raw stream stops when the host goes quiet", "[io][raw]")
{
    Rig rig(true);
    rig.setPin(0);
    rig.send(streamRequest(kPin, true));

    // Keepalive every 5 s holds it open.
    for (int i = 0; i < 3; i++)
    {
        rig.run(5000);
        REQUIRE(rig.io.isRawStreaming());
        rig.send(streamRequest(kPin, true));
    }

    rig.run(enomik::IO::RAW_STREAM_TIMEOUT_MS + 100);
    REQUIRE_FALSE(rig.io.isRawStreaming());
}

TEST_CASE("raw stream on an unknown pin answers PIN_NOT_FOUND", "[io][raw]")
{
    Rig rig(true);
    rig.sysex.clear();
    rig.send(streamRequest(33, true));
    REQUIRE_FALSE(rig.io.isRawStreaming());
    REQUIRE(rig.sysex.size() == 1);
    REQUIRE(rig.sysex[0][4] == static_cast<uint8_t>(enomik::SysExCommand::ERROR_RESPONSE));
    REQUIRE(rig.sysex[0][6] == static_cast<uint8_t>(enomik::SysExErrorCode::PIN_NOT_FOUND));
}
