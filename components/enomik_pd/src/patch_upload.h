// Private: receives patch files over SysEx (MIDI File Dump), from USB MIDI and
// ESP-NOW alike, and writes them into the patch directory. Runs on the Pd
// thread (called from the MIDI bridge's SysEx handler and the Pd loop).
//
// Protocol: MIDI File Dump (universal non-real-time 0x7E, sub-ID 0x07), see
// components/enomik_pd/README.md "Patch upload".
#pragma once

#include <stdint.h>
#include <stdio.h>

namespace enomik
{
    namespace pd
    {
        namespace detail
        {
            class PatchUpload
            {
            public:
                using SendFn = void (*)(const uint8_t *data, uint16_t length);
                using ReloadFn = void (*)();

                void configure(const char *dir, const char *mainFile, uint8_t deviceId,
                               SendFn send, ReloadFn reload);

                /**
                 * @return true when the message belongs to the upload protocol
                 * (addressed to us or not): it must not reach Pd's [sysexin].
                 */
                bool handle(const uint8_t *data, unsigned length);

                /** Aborts a stalled transfer. Call once per Pd buffer. */
                void poll();

            private:
                void onHeader(const uint8_t *p, unsigned n);
                void onData(const uint8_t *p, unsigned n);
                void onEndOfFile(uint8_t packet);
                void abort(const char *why);
                void sendHandshake(uint8_t subId, uint8_t packet);

                const char *_dir = nullptr;
                const char *_mainFile = nullptr;
                uint8_t _deviceId = 0;
                SendFn _send = nullptr;
                ReloadFn _reload = nullptr;

                FILE *_file = nullptr;
                char _name[48] = {};
                char _tmpPath[96] = {};
                char _finalPath[96] = {};
                uint32_t _expected = 0;
                uint32_t _received = 0;
                uint8_t _nextPacket = 0;
                int64_t _lastActivityUs = 0;
            };
        }
    }
}
