#pragma once
// TinyUSB / Arduino MIDI subset so enomik::Dongle compiles with HAS_USB_MIDI on
// the host (native_dongle_usb_tests only: a separate executable, because the
// Dongle class differs from the one the other tests build without USB).
// Packets written to the USB host are recorded; the SysEx handler the Dongle
// registers can be called to fake SysEx from the host.
#include <array>
#include <cstdint>
#include <vector>

#define MIDI_CHANNEL_OMNI 0

struct TinyUSBDeviceStub
{
    bool isMounted = true;
    bool isSuspended = false;

    bool mounted() const { return isMounted; }
    bool suspended() const { return isSuspended; }
    bool ready() const { return isMounted && !isSuspended; }
    bool attach() { return true; }
    bool detach() { return true; }
    void setManufacturerDescriptor(const char *) {}
    void setProductDescriptor(const char *) {}
    void setSerialDescriptor(const char *) {}
};
inline TinyUSBDeviceStub TinyUSBDevice;

struct DongleUsbMidiStub
{
    std::vector<std::array<uint8_t, 4>> packets;

    bool begin() { return true; }
    bool writePacket(const uint8_t packet[4])
    {
        packets.push_back({packet[0], packet[1], packet[2], packet[3]});
        return true;
    }
};
inline DongleUsbMidiStub g_dongle_usb_midi;

struct DongleMidiStub
{
    void (*sysExHandler)(uint8_t *data, unsigned length) = nullptr;

    void begin(int) {}
    void turnThruOff() {}
    bool read() { return false; }
    template <typename Callback>
    void setHandleNoteOn(Callback) {}
    template <typename Callback>
    void setHandleNoteOff(Callback) {}
    template <typename Callback>
    void setHandleControlChange(Callback) {}
    template <typename Callback>
    void setHandleProgramChange(Callback) {}
    template <typename Callback>
    void setHandlePitchBend(Callback) {}
    template <typename Callback>
    void setHandleAfterTouchChannel(Callback) {}
    template <typename Callback>
    void setHandleAfterTouchPoly(Callback) {}
    template <typename Callback>
    void setHandleStart(Callback) {}
    template <typename Callback>
    void setHandleStop(Callback) {}
    template <typename Callback>
    void setHandleContinue(Callback) {}
    template <typename Callback>
    void setHandleClock(Callback) {}
    template <typename Callback>
    void setHandleSongPosition(Callback) {}
    template <typename Callback>
    void setHandleSongSelect(Callback) {}
    void setHandleSystemExclusive(void (*handler)(uint8_t *, unsigned)) { sysExHandler = handler; }
};
inline DongleMidiStub DONGLE_USBMIDI;

inline bool tud_remote_wakeup() { return true; }
inline const char *esp_get_idf_version() { return "host"; }
inline void delay(uint32_t ms) { millisNow() += ms; }
