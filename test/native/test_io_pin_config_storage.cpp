// enomik::IO pin configs: stored in NVS and restored after a reboot,
// including the touch threshold.

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include "arduino_stubs.h"
#include "client_host_stubs.h"
#include "enomik_client.h"

namespace {

constexpr uint8_t kPin = 4;

// Asks the IO for one pin config over SysEx, returns the response bytes.
std::vector<uint8_t> getPinConfig(enomik::IO &io, uint8_t pin)
{
    std::vector<uint8_t> response;
    io.setOnSysExSendRequest([&](midi_sysex_message msg) {
        response.assign(msg.data, msg.data + msg.length);
    });
    const auto request = enomik::SysExEncoder::encodeByteResponse(enomik::SysExCommand::GET_PIN_CONFIG, pin);
    io.onSysEx(request.data, request.length);
    return response;
}

struct PrefsScope
{
    PrefsScope()
    {
        stubPrefsPersist() = true;
        stubPrefsBlobs().clear();
    }
    ~PrefsScope()
    {
        stubPrefsPersist() = false;
        stubPrefsBlobs().clear();
    }
};

} // namespace

TEST_CASE("touch threshold survives a reboot", "[io][prefs]")
{
    PrefsScope prefs;
    {
        enomik::IO io;
        io.begin();
        enomik::PinConfig cfg(kPin, enomik::ENOMIK_INPUT_TOUCH);
        cfg.threshold = 40;
        cfg.midi_channel = 3;
        cfg.midi_type = MidiStatus::MIDI_NOTE_ON;
        cfg.midi_note = 62;
        const auto sysex = enomik::SysExEncoder::encodePinConfig(cfg, enomik::SysExCommand::SET_PIN_CONFIG);
        io.onSysEx(sysex.data, sysex.length);
    }

    enomik::IO rebooted;
    rebooted.begin();
    const auto response = getPinConfig(rebooted, kPin);
    REQUIRE(response.size() > 10);
    REQUIRE(response[5] == kPin);
    REQUIRE(response[6] == enomik::ENOMIK_INPUT_TOUCH);
    REQUIRE(response[7] == 40); // threshold
    REQUIRE(response[8] == 3);  // channel
    REQUIRE(response[10] == 62);
}

TEST_CASE("pin config stored before 0.19.0 (8 bytes) still loads, threshold 0", "[io][prefs]")
{
    PrefsScope prefs;
    const uint32_t count = 1;
    stubPrefsBlobs()["pinconfigs/count"] = std::string(reinterpret_cast<const char *>(&count), sizeof(count));
    const uint8_t old[8] = {kPin, enomik::ENOMIK_ANALOG_INPUT, 2, static_cast<uint8_t>(MidiStatus::MIDI_CONTROL_CHANGE),
                            21, 0, 0, 127};
    stubPrefsBlobs()["pinconfigs/cfg0"] = std::string(reinterpret_cast<const char *>(old), sizeof(old));

    enomik::IO io;
    io.begin();
    const auto response = getPinConfig(io, kPin);
    REQUIRE(response.size() > 10);
    REQUIRE(response[5] == kPin);
    REQUIRE(response[6] == enomik::ENOMIK_ANALOG_INPUT);
    REQUIRE(response[7] == 0); // threshold
    REQUIRE(response[8] == 2); // channel
    REQUIRE(response[10] == 21);
}
