#include "midi_bridge.h"

#include "enomik_client.h"
#include "enomik_pd.h"
#include "esp_log.h"
#include "z_libpd.h"

static const char *TAG = "enomik_pd";

static enomik::Client g_client;
static enomik::pd::detail::SysExInterceptor g_sysexInterceptor = nullptr;

// enomik uses MIDI channels 1-16, libpd 0-15 (plus port * 16, ignored here).
static inline int toPd(byte channel) { return (channel - 1) & 0x0F; }
static inline byte toEnomik(int pdChannel) { return static_cast<byte>((pdChannel & 0x0F) + 1); }

// ---------------------------------------------------------------------------
// ESP-NOW / USB -> Pd
// ---------------------------------------------------------------------------

static void installEnomikHandlers()
{
    g_client.setHandleNoteOn([](byte ch, byte note, byte vel)
                             { libpd_noteon(toPd(ch), note, vel); });
    // Pd has no separate note-off: [notein] reports velocity 0.
    g_client.setHandleNoteOff([](byte ch, byte note, byte)
                              { libpd_noteon(toPd(ch), note, 0); });
    g_client.setHandleControlChange([](byte ch, byte cc, byte value)
                                    { libpd_controlchange(toPd(ch), cc, value); });
    g_client.setHandleProgramChange([](byte ch, byte program)
                                    { libpd_programchange(toPd(ch), program); });
    g_client.setHandlePitchBend([](byte ch, int value) // signed, center 0 (same as libpd)
                                { libpd_pitchbend(toPd(ch), value); });
    g_client.setHandleAfterTouchChannel([](byte ch, byte pressure)
                                        { libpd_aftertouch(toPd(ch), pressure); });
    g_client.setHandleAfterTouchPoly([](byte ch, byte note, byte pressure)
                                     { libpd_polyaftertouch(toPd(ch), note, pressure); });

    // System realtime -> [midirealtimein]
    g_client.setHandleClock([]
                            { libpd_sysrealtime(0, 0xF8); });
    g_client.setHandleStart([]
                            { libpd_sysrealtime(0, 0xFA); });
    g_client.setHandleContinue([]
                               { libpd_sysrealtime(0, 0xFB); });
    g_client.setHandleStop([]
                           { libpd_sysrealtime(0, 0xFC); });

    // System common -> [midiin]
    g_client.setHandleSongPosition([](uint16_t pos)
                                   {
        libpd_midibyte(0, 0xF2);
        libpd_midibyte(0, pos & 0x7F);
        libpd_midibyte(0, (pos >> 7) & 0x7F); });
    g_client.setHandleSongSelect([](byte song)
                                 {
        libpd_midibyte(0, 0xF3);
        libpd_midibyte(0, song & 0x7F); });

    // SysEx -> [sysexin], framed F0 ... F7
    g_client.setHandleSysEx([](uint8_t *data, unsigned int length)
                            {
        if (!data || length == 0)
            return;
        if (g_sysexInterceptor && g_sysexInterceptor(data, length))
            return; // e.g. patch upload: not for the patch
        if (data[0] != 0xF0)
            libpd_sysex(0, 0xF0);
        for (unsigned int i = 0; i < length; ++i)
            libpd_sysex(0, data[i]);
        if (data[length - 1] != 0xF7)
            libpd_sysex(0, 0xF7); });
}

// ---------------------------------------------------------------------------
// Pd -> ESP-NOW / USB
// ---------------------------------------------------------------------------

static void onPdNoteOn(int ch, int pitch, int vel)
{
    // Keep Pd's convention: velocity 0 is sent as Note On, which every
    // receiver treats as Note Off.
    g_client.sendNoteOn(pitch, vel, toEnomik(ch));
}

static void onPdControlChange(int ch, int cc, int value)
{
    g_client.sendControlChange(cc, value, toEnomik(ch));
}

static void onPdProgramChange(int ch, int program)
{
    g_client.sendProgramChange(program, toEnomik(ch));
}

static void onPdPitchBend(int ch, int value) // libpd: -8192..8191
{
    g_client.sendPitchBend(value, toEnomik(ch));
}

static void onPdAfterTouch(int ch, int pressure)
{
    g_client.sendAfterTouch(pressure, toEnomik(ch));
}

static void onPdPolyAfterTouch(int ch, int note, int pressure)
{
    g_client.sendPolyAfterTouch(note, pressure, toEnomik(ch));
}

// Raw bytes from [midiout], [sysexout] and [midirealtimeout] (Pd >= 0.54).
// Reassembled into whole messages before handing them to enomik.
namespace
{
    constexpr size_t kSysExMax = 1024; // enomik::Client::sendSysEx limit

    struct ByteParser
    {
        uint8_t sysex[kSysExMax];
        size_t sysexLen = 0;
        bool inSysEx = false;
        bool sysexOverflow = false;

        uint8_t status = 0; // running status
        uint8_t data[2];
        int dataLen = 0;

        static int dataBytesFor(uint8_t st)
        {
            switch (st & 0xF0)
            {
            case 0xC0:
            case 0xD0:
                return 1;
            case 0xF0:
                return st == 0xF2 ? 2 : (st == 0xF1 || st == 0xF3) ? 1
                                                                    : 0;
            default:
                return 2;
            }
        }

        void realtime(uint8_t b)
        {
            switch (b)
            {
            case 0xF8: g_client.sendClock(); break;
            case 0xFA: g_client.sendStart(); break;
            case 0xFB: g_client.sendContinue(); break;
            case 0xFC: g_client.sendStop(); break;
            default: break; // active sensing, reset, undefined: not forwarded
            }
        }

        void message()
        {
            const byte ch = static_cast<byte>((status & 0x0F) + 1);
            switch (status & 0xF0)
            {
            case 0x80: g_client.sendNoteOff(data[0], data[1], ch); break;
            case 0x90: g_client.sendNoteOn(data[0], data[1], ch); break;
            case 0xA0: g_client.sendPolyAfterTouch(data[0], data[1], ch); break;
            case 0xB0: g_client.sendControlChange(data[0], data[1], ch); break;
            case 0xC0: g_client.sendProgramChange(data[0], ch); break;
            case 0xD0: g_client.sendAfterTouch(data[0], ch); break;
            case 0xE0: g_client.sendPitchBend(((data[1] << 7) | data[0]) - 8192, ch); break;
            case 0xF0:
                if (status == 0xF2)
                    g_client.sendSongPosition(static_cast<uint16_t>((data[1] << 7) | data[0]));
                else if (status == 0xF3)
                    g_client.sendSongSelect(data[0]);
                break; // F1 (MTC quarter frame), F6: not supported by enomik
            }
        }

        void feed(uint8_t b)
        {
            if (b >= 0xF8)
            {
                realtime(b); // may interleave anything, does not touch state
                return;
            }
            if (b == 0xF0)
            {
                inSysEx = true;
                sysexOverflow = false;
                sysexLen = 0;
                sysex[sysexLen++] = b;
                status = 0;
                return;
            }
            if (inSysEx)
            {
                if (b == 0xF7 || b >= 0x80)
                {
                    inSysEx = false;
                    if (sysexOverflow)
                    {
                        ESP_LOGW(TAG, "SysEx longer than %u bytes dropped", (unsigned)kSysExMax);
                    }
                    else if (sysexLen < kSysExMax)
                    {
                        sysex[sysexLen++] = 0xF7;
                        g_client.sendSysEx(sysex, static_cast<uint16_t>(sysexLen));
                    }
                    if (b == 0xF7)
                        return;
                    // any other status byte ends SysEx and starts a new message
                }
                else
                {
                    if (sysexLen < kSysExMax - 1)
                        sysex[sysexLen++] = b;
                    else
                        sysexOverflow = true;
                    return;
                }
            }
            if (b & 0x80)
            {
                status = b;
                dataLen = 0;
                if (dataBytesFor(status) == 0)
                {
                    status = 0; // F6 tune request, stray F7: ignore
                }
                return;
            }
            if (!status)
                return; // data byte without status
            data[dataLen++] = b;
            if (dataLen >= dataBytesFor(status))
            {
                message();
                dataLen = 0;
                if (status >= 0xF0)
                    status = 0; // no running status for system common
            }
        }
    };

    ByteParser g_byteParser;
}

static void onPdMidiByte(int /*port*/, int byte)
{
    g_byteParser.feed(static_cast<uint8_t>(byte));
}

// ---------------------------------------------------------------------------

namespace enomik
{
    namespace pd
    {
        Client &client()
        {
            return g_client;
        }

        namespace detail
        {
            bool clientBegin(const char *name)
            {
                installEnomikHandlers();
                if (name)
                    g_client.setName(name);
                if (!g_client.begin())
                {
                    ESP_LOGE(TAG, "enomik::Client::begin() failed");
                    return false;
                }
                ESP_LOGI(TAG, "enomik client up, peers=%d", g_client.getPeerCount());
                return true;
            }

            void installPdHooks()
            {
                libpd_set_noteonhook(onPdNoteOn);
                libpd_set_controlchangehook(onPdControlChange);
                libpd_set_programchangehook(onPdProgramChange);
                libpd_set_pitchbendhook(onPdPitchBend);
                libpd_set_aftertouchhook(onPdAfterTouch);
                libpd_set_polyaftertouchhook(onPdPolyAfterTouch);
                libpd_set_midibytehook(onPdMidiByte);
            }

            void poll()
            {
                g_client.loop();
            }

            void setSysExInterceptor(SysExInterceptor fn)
            {
                g_sysexInterceptor = fn;
            }

            void sendSysEx(const uint8_t *data, uint16_t length)
            {
                g_client.sendSysEx(data, length);
            }
        }
    }
}
