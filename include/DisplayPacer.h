#pragma once

#include <cstdint>

namespace enomik
{

/**
 * @brief Decides when the dongle may redraw its display.
 *
 * A redraw runs in loop() and can take tens of milliseconds over I2C, while
 * no USB or ESP-NOW MIDI is bridged. So it waits until MIDI was quiet for
 * quietMs, but at most maxDeferMs, so the display still follows continuous
 * traffic. A redraw requested by UI input (requestNow()) is not held back.
 * Times in milliseconds; wrap-safe.
 */
class DisplayPacer
{
public:
    uint32_t intervalMs = 200; ///< Minimum time between two redraws.
    uint32_t quietMs = 100;    ///< Redraw only after MIDI was quiet this long (0 = always).
    uint32_t maxDeferMs = 1000; ///< ...unless the last redraw is this old (0 = wait for quiet).

    /** @brief MIDI was bridged at @p now. */
    void noteMidi(uint32_t now)
    {
        _lastMidiMs = now;
        _midiSeen = true;
    }

    /** @brief Next due() returns true right away (e.g. after a button press). */
    void requestNow() { _urgent = true; }

    /** @brief Whether to check for changes and redraw now. */
    bool due(uint32_t now) const
    {
        if (_urgent)
        {
            return true;
        }
        const uint32_t sinceDraw = now - _lastDrawMs;
        if (sinceDraw < intervalMs)
        {
            return false;
        }
        const bool busy = _midiSeen && quietMs > 0 && now - _lastMidiMs < quietMs;
        return !busy || (maxDeferMs > 0 && sinceDraw >= maxDeferMs);
    }

    /** @brief Call after a due() check that went ahead. */
    void drawn(uint32_t now)
    {
        _lastDrawMs = now;
        _urgent = false;
    }

private:
    uint32_t _lastDrawMs = 0;
    uint32_t _lastMidiMs = 0;
    bool _midiSeen = false;
    bool _urgent = true;
};

} // namespace enomik
