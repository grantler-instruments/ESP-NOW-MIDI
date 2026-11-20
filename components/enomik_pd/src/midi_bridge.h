// Private: bridge between enomik::Client and Pd's MIDI objects (libpd).
//
//   ESP-NOW / USB -> enomik handlers -> libpd_noteon()...  -> [notein] [ctlin] ...
//   [noteout] [ctlout] [midiout] ... -> libpd hooks -> Client::send*() -> ESP-NOW / USB
//
// Everything except clientBegin() runs on the Pd task. enomik dispatches
// received MIDI from poll() (Client::loop()), so handlers run there too.
#pragma once

#include <stdint.h>

namespace enomik
{
    namespace pd
    {
        namespace detail
        {
            bool clientBegin(const char *name); // app task, before the Pd task starts
            void installPdHooks();              // Pd task, after libpd_init()
            void poll();                        // Pd task, once per buffer

            /** Sees each received SysEx first; returning true keeps it from [sysexin]. */
            using SysExInterceptor = bool (*)(const uint8_t *data, unsigned length);
            void setSysExInterceptor(SysExInterceptor fn);

            /** Sends SysEx (with F0/F7) over ESP-NOW and USB. Pd task only. */
            void sendSysEx(const uint8_t *data, uint16_t length);
        }
    }
}
