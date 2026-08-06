#pragma once
/**
 * @file esp_now_midi_usb.h
 * @brief USB MIDI backend for enomik_dongle.h: Adafruit TinyUSB on Arduino,
 *        raw tinyusb on ESP-IDF.
 *
 * The ESP-IDF branch mirrors only the call surface enomik_dongle.h actually
 * uses (a `TinyUSBDevice`-like object, a `DONGLE_USBMIDI`-like MIDI object,
 * and the raw `g_dongle_usb_midi.writePacket()` used for sends) - it is not a
 * general-purpose MIDI library.
 *
 * HIGHEST-RISK file in this port: no local IDF SDK to build-verify against,
 * and unlike GPIO/ADC/touch, a USB descriptor mistake usually means the
 * device fails to enumerate at all rather than misbehaving partially.
 *
 * Uses the `espressif/esp_tinyusb` managed component's own install path
 * (`tinyusb_driver_install()` + `tinyusb_config_t`) rather than calling raw
 * tinyusb (`tusb_init()`) directly: esp_tinyusb is the only way to get
 * tinyusb on IDF at all, and it ships its own `tud_descriptor_*_cb()`
 * implementations (driven by `tinyusb_config_t`) - defining our own would be
 * a duplicate-symbol link error. `tinyusb_config_t`'s exact fields are
 * unverified against a real build; reconcile against
 * `managed_components/espressif__esp_tinyusb/include/tinyusb.h` (or
 * `$IDF_PATH/examples/peripherals/usb/device/tusb_midi` if using raw
 * tinyusb instead) if this doesn't compile as-is.
 */

#ifdef ARDUINO
#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#elif defined(ESP_PLATFORM)

#include "tinyusb.h"
#include "class/midi/midi_device.h"
#include "./esp_now_midi_log.h"
#include <cstdint>
#include <cstring>
#include <functional>

#ifndef MIDI_CHANNEL_OMNI
#define MIDI_CHANNEL_OMNI 0
#endif

namespace esp_now_midi_usb_detail
{

enum
{
    ITF_NUM_MIDI_AUDIO_CONTROL,
    ITF_NUM_MIDI_STREAMING,
    ITF_NUM_TOTAL
};

enum
{
    EPNUM_MIDI_OUT = 0x01,
    EPNUM_MIDI_IN = 0x81,
};

// esp_tinyusb's string_descriptor array convention: index 0 is the special
// raw language-ID encoding, the rest are plain C strings it UTF-16-encodes
// itself (see the note on usbInstall() below - this is the part most likely
// to need correction against the real esp_tinyusb header).
enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_COUNT
};

#define ENOMIK_USB_MIDI_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

inline const char *&manufacturerString()
{
    static const char *s = "enomik";
    return s;
}

inline const char *&productString()
{
    static const char *s = "enomik USB MIDI";
    return s;
}

// --- Handler storage (mirrors the 13 setHandleX() registrations enomik_dongle.h makes) ---

struct HandlerTable
{
    std::function<void(uint8_t, uint8_t, uint8_t)> noteOn;
    std::function<void(uint8_t, uint8_t, uint8_t)> noteOff;
    std::function<void(uint8_t, uint8_t, uint8_t)> controlChange;
    std::function<void(uint8_t, uint8_t)> programChange;
    std::function<void(uint8_t, int)> pitchBend;
    std::function<void(uint8_t, uint8_t)> afterTouchChannel;
    std::function<void(uint8_t, uint8_t, uint8_t)> afterTouchPoly;
    std::function<void()> start;
    std::function<void()> stop;
    std::function<void()> continueMidi;
    std::function<void()> clock;
    std::function<void(unsigned int)> songPosition;
    std::function<void(uint8_t)> songSelect;
};

inline HandlerTable &handlers()
{
    static HandlerTable table;
    return table;
}

// Dispatches one raw 4-byte USB-MIDI event packet to the registered handlers.
// Mirrors (in reverse) enomik_dongle.h's own sendQueuedMidi() encode table,
// so the Code Index Number (CIN) mapping is cross-checked against code
// already known to work, not just the USB-MIDI spec from memory.
inline void dispatchPacket(const uint8_t packet[4])
{
    const uint8_t cin = packet[0] & 0x0F;
    const uint8_t status = packet[1];
    const uint8_t channel = static_cast<uint8_t>((status & 0x0F) + 1); // 0-15 -> 1-16
    HandlerTable &h = handlers();

    switch (cin)
    {
    case 0x8: // Note Off
        if (h.noteOff)
            h.noteOff(channel, packet[2], packet[3]);
        break;
    case 0x9: // Note On
        if (h.noteOn)
            h.noteOn(channel, packet[2], packet[3]);
        break;
    case 0xA: // Poly Aftertouch
        if (h.afterTouchPoly)
            h.afterTouchPoly(channel, packet[2], packet[3]);
        break;
    case 0xB: // Control Change
        if (h.controlChange)
            h.controlChange(channel, packet[2], packet[3]);
        break;
    case 0xC: // Program Change
        if (h.programChange)
            h.programChange(channel, packet[2]);
        break;
    case 0xD: // Channel Aftertouch
        if (h.afterTouchChannel)
            h.afterTouchChannel(channel, packet[2]);
        break;
    case 0xE: // Pitch Bend
        if (h.pitchBend)
        {
            const int value = ((packet[3] << 7) | packet[2]) - 8192;
            h.pitchBend(channel, value);
        }
        break;
    case 0xF: // System common / real-time (single-byte or fixed-length)
        switch (status)
        {
        case 0xF2: // Song Position Pointer
            if (h.songPosition)
                h.songPosition(static_cast<unsigned int>((packet[3] << 7) | packet[2]));
            break;
        case 0xF3: // Song Select
            if (h.songSelect)
                h.songSelect(packet[2]);
            break;
        case 0xF8: // Timing Clock
            if (h.clock)
                h.clock();
            break;
        case 0xFA: // Start
            if (h.start)
                h.start();
            break;
        case 0xFB: // Continue
            if (h.continueMidi)
                h.continueMidi();
            break;
        case 0xFC: // Stop
            if (h.stop)
                h.stop();
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

// --- USB descriptors ---------------------------------------------------------
//
// Built with TinyUSB's own TUD_CONFIG_DESCRIPTOR/TUD_MIDI_DESCRIPTOR macros
// (the same ones tinyusb's bundled midi_test example and Adafruit_USBD_MIDI
// use internally) rather than hand-written descriptor bytes, specifically to
// avoid inventing the Audio Control / MIDIStreaming interface layout from
// scratch.

inline const tusb_desc_device_t &deviceDescriptor()
{
    static const tusb_desc_device_t desc = {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor = 0x303A, // Espressif VID
        .idProduct = 0x8121,
        .bcdDevice = 0x0100,
        .iManufacturer = STRID_MANUFACTURER,
        .iProduct = STRID_PRODUCT,
        .iSerialNumber = STRID_SERIAL,
        .bNumConfigurations = 0x01,
    };
    return desc;
}

inline const uint8_t *configDescriptor()
{
    static const uint8_t desc[] = {
        TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, ENOMIK_USB_MIDI_CONFIG_TOTAL_LEN,
                               0x00, 100),
        TUD_MIDI_DESCRIPTOR(ITF_NUM_MIDI_AUDIO_CONTROL, STRID_PRODUCT,
                             EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
    };
    return desc;
}

inline bool &usbInstalled()
{
    static bool installed = false;
    return installed;
}

// Brings up USB device mode via esp_tinyusb's own installer, which owns
// descriptor callbacks (tud_descriptor_*_cb - do NOT also define these
// ourselves, esp_tinyusb's descriptors_control.c already does), ESP32
// PHY/clock bring-up, and the tud_task() polling task internally.
//
// tinyusb_config_t verified against a real build (IDF v6.0.1 / esp_tinyusb
// managed component, examples_idf/dongle/managed_components/): it nests
// port/phy/task/descriptor/event_cb, not the flat shape this originally
// guessed - see include/esp_now_midi_usb.h history if that ever regresses.
inline void usbInstall()
{
    if (usbInstalled())
    {
        return;
    }

    static const char langIdBytes[2] = {0x09, 0x04}; // English (US), esp_tinyusb's raw-byte convention
    static const char *stringDescriptors[STRID_COUNT] = {
        langIdBytes, // 0: language ID
        manufacturerString(),
        productString(),
        "0", // serial
    };

    tinyusb_config_t cfg = {};
    cfg.port = TINYUSB_PORT_FULL_SPEED_0;
    // phy: leave zero-initialized (skip_setup=false) - esp_tinyusb configures
    // the internal USB PHY automatically, which is what we want.
    cfg.task.size = 4096;
    cfg.task.priority = 5;
    cfg.task.xCoreID = 0;
    cfg.descriptor.device = &deviceDescriptor();
    cfg.descriptor.qualifier = nullptr; // full-speed only, no high-speed qualifier needed
    cfg.descriptor.string = stringDescriptors;
    cfg.descriptor.string_count = STRID_COUNT;
    cfg.descriptor.full_speed_config = configDescriptor();
    cfg.descriptor.high_speed_config = nullptr;

    tinyusb_driver_install(&cfg);
    usbInstalled() = true;
}

} // namespace esp_now_midi_usb_detail

// --- TinyUSBDevice-equivalent ------------------------------------------------

class TinyUSBDeviceClass
{
public:
    void setManufacturerDescriptor(const char *s)
    {
        if (s)
            esp_now_midi_usb_detail::manufacturerString() = s;
    }

    void setProductDescriptor(const char *s)
    {
        if (s)
            esp_now_midi_usb_detail::productString() = s;
    }

    bool mounted() { return tud_mounted(); }
    bool suspended() { return tud_suspended(); }
    bool ready() { return tud_ready(); }

    void attach()
    {
        esp_now_midi_usb_detail::usbInstall();
        tud_connect();
    }

    void detach() { tud_disconnect(); }
    void remoteWakeup() { tud_remote_wakeup(); }
};

inline TinyUSBDeviceClass TinyUSBDevice;

// --- DONGLE_USBMIDI-equivalent MIDI object -----------------------------------
//
// enomik_dongle.h never calls sendX() on this object (it sends via the raw
// writePacket() below), so this only needs to support receive: begin/read
// plus the 13 setHandleX() registrations.

class TinyUsbMidiClass
{
public:
    void begin(int /*channel*/ = MIDI_CHANNEL_OMNI) {}
    void turnThruOff() {}

    void read()
    {
        uint8_t packet[4];
        while (tud_midi_available())
        {
            if (!tud_midi_packet_read(packet))
            {
                break;
            }
            esp_now_midi_usb_detail::dispatchPacket(packet);
        }
    }

    bool writePacket(const uint8_t packet[4]) { return tud_midi_packet_write(packet); }

    void setHandleNoteOn(std::function<void(uint8_t, uint8_t, uint8_t)> cb) { esp_now_midi_usb_detail::handlers().noteOn = cb; }
    void setHandleNoteOff(std::function<void(uint8_t, uint8_t, uint8_t)> cb) { esp_now_midi_usb_detail::handlers().noteOff = cb; }
    void setHandleControlChange(std::function<void(uint8_t, uint8_t, uint8_t)> cb) { esp_now_midi_usb_detail::handlers().controlChange = cb; }
    void setHandleProgramChange(std::function<void(uint8_t, uint8_t)> cb) { esp_now_midi_usb_detail::handlers().programChange = cb; }
    void setHandlePitchBend(std::function<void(uint8_t, int)> cb) { esp_now_midi_usb_detail::handlers().pitchBend = cb; }
    void setHandleAfterTouchChannel(std::function<void(uint8_t, uint8_t)> cb) { esp_now_midi_usb_detail::handlers().afterTouchChannel = cb; }
    void setHandleAfterTouchPoly(std::function<void(uint8_t, uint8_t, uint8_t)> cb) { esp_now_midi_usb_detail::handlers().afterTouchPoly = cb; }
    void setHandleStart(std::function<void()> cb) { esp_now_midi_usb_detail::handlers().start = cb; }
    void setHandleStop(std::function<void()> cb) { esp_now_midi_usb_detail::handlers().stop = cb; }
    void setHandleContinue(std::function<void()> cb) { esp_now_midi_usb_detail::handlers().continueMidi = cb; }
    void setHandleClock(std::function<void()> cb) { esp_now_midi_usb_detail::handlers().clock = cb; }
    void setHandleSongPosition(std::function<void(unsigned int)> cb) { esp_now_midi_usb_detail::handlers().songPosition = cb; }
    void setHandleSongSelect(std::function<void(uint8_t)> cb) { esp_now_midi_usb_detail::handlers().songSelect = cb; }
};

// Mirrors Adafruit_USBD_MIDI's role: the object writePacket() is called on.
class TinyUsbRawMidiClass
{
public:
    void begin() {}
    bool writePacket(const uint8_t packet[4]) { return tud_midi_packet_write(packet); }
};

#endif
