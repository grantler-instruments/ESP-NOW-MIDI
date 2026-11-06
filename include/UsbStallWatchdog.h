#pragma once

#include <cstdint>

namespace enomik
{

/** @brief What the USB watchdog is allowed to do. */
enum class UsbWatchdogMode : uint8_t
{
    Off,     ///< Only rate-limited remote wakeup; never re-attaches.
    Observe, ///< Detects and counts stalls, but never re-attaches.
    Recover, ///< Re-attaches USB (software replug) to recover from a stall.
};

/** @brief Action the caller must perform after UsbStallWatchdog::tick(). */
enum class UsbWatchdogAction : uint8_t
{
    None,
    RemoteWakeup, ///< Call tud_remote_wakeup() and report the result.
    Detach,       ///< Disconnect from the bus (TinyUSBDevice.detach()).
    Attach,       ///< Reconnect to the bus (TinyUSBDevice.attach()).
};

/** @brief Timing and policy of the watchdog. Times in milliseconds. */
struct UsbWatchdogConfig
{
    /** IN endpoint busy this long with no new write accepted, while the bus is
     *  awake = host stopped reading. Writes being accepted count as progress, so
     *  a host that reads under heavy traffic never trips it. */
    uint32_t stallMs = 500;
    /** Minimum interval between remote wakeup attempts the host accepted. */
    uint32_t wakeRetryMs = 1000;
    /** How long the device stays detached (hosts debounce for ~100 ms). */
    uint32_t detachMs = 150;
    /** No stall recovery while the host sent MIDI within this window. */
    uint32_t hostActiveGuardMs = 1000;
    /** Wait before the 2nd attempt when an attempt delivered nothing; doubles. */
    uint32_t backoffBaseMs = 30000;
    /** Upper bound for the backoff. */
    uint32_t backoffMaxMs = 600000;
    /** After re-attach, the recovery window ends at the first delivery or after
     *  this long (the caller pauses stale-dropping during the window). */
    uint32_t recoveryWindowMs = 3000;
    /** Re-attach when suspended with data waiting and the host refuses remote
     *  wakeup (at most once per suspend). Note: a computer that is asleep looks
     *  the same to the device; a re-attach then makes it re-enumerate the
     *  dongle on wake. */
    bool recoverFromSuspend = true;
};

/** @brief Inputs sampled by the caller once per loop. */
struct UsbWatchdogInputs
{
    uint32_t now = 0;           ///< millis()
    bool mounted = false;       ///< tud_mounted()
    bool suspended = false;     ///< tud_suspended()
    bool pending = false;       ///< messages waiting in the dongle's USB queue
    bool txBlocked = false;     ///< data handed to USB that the host has not read yet
    bool wrote = false;         ///< a packet was handed to USB since the previous tick
    bool endpointKnown = false; ///< txBlocked comes from the real IN endpoint state
};

/** @brief Counters for diagnostics. */
struct UsbHealthStats
{
    uint32_t suspends = 0;         ///< bus suspends seen while mounted
    uint32_t longestSuspendMs = 0; ///< longest single suspend
    uint32_t wakeupsTried = 0;     ///< remote wakeup attempts
    uint32_t wakeupsRefused = 0;   ///< attempts the host had not enabled
    uint32_t stalls = 0;           ///< episodes of IN endpoint busy >= stallMs
    uint32_t longestBusyMs = 0;    ///< longest IN endpoint busy without progress (awake)
    uint32_t reattaches = 0;       ///< re-attaches done (Recover) or due (Observe)
    uint32_t recoveries = 0;       ///< re-attaches followed by a delivery
};

/**
 * @brief Detects a USB host that stopped taking MIDI and recovers by re-attaching.
 *
 * Pure logic, no hardware access: the caller samples UsbWatchdogInputs every
 * loop, calls tick(), and performs the returned action. This keeps every
 * decision unit-testable.
 *
 * Re-attach triggers (only ever while data is waiting for the host):
 * - Stall: awake bus, IN endpoint busy >= stallMs without progress, the host
 *   has read from the dongle earlier in this connection (so an app is known to
 *   listen; a host that never reads, e.g. no app has the input open, is left
 *   alone), and no MIDI from the host within hostActiveGuardMs. Needs the real
 *   endpoint state (endpointKnown).
 * - Suspend: remote wakeup is requested once (then every wakeRetryMs if the
 *   host accepts it). If the host refuses it -> re-attach, at most once per
 *   suspend (recoverFromSuspend).
 *
 * Loop protection: an attempt that is not followed by a delivery makes the
 * next attempt wait backoffBaseMs, doubling up to backoffMaxMs. Any delivery
 * re-arms immediately. Detach is always followed by Attach after detachMs.
 */
class UsbStallWatchdog
{
public:
    void setMode(UsbWatchdogMode mode) { _mode = mode; }
    UsbWatchdogMode mode() const { return _mode; }

    UsbWatchdogConfig &config() { return _cfg; }
    const UsbWatchdogConfig &config() const { return _cfg; }

    const UsbHealthStats &stats() const { return _stats; }

    /** @brief Call whenever MIDI arrives from the host. */
    void noteHostRx(uint32_t now)
    {
        _lastHostRxMs = now;
        _hostRxValid = true;
    }

    /** @brief Report the result of a RemoteWakeup action (tud_remote_wakeup()). */
    void noteWakeupResult(bool accepted)
    {
        if (!accepted)
        {
            ++_stats.wakeupsRefused;
            _wakeRefused = true;
        }
    }

    /** @return true while detached, or re-attached and waiting for the first delivery. */
    bool isRecovering(uint32_t now) const
    {
        if (_phase == Phase::Detached)
        {
            return true;
        }
        return _phase == Phase::Reattached && elapsed(now, _reattachedAt) < _cfg.recoveryWindowMs;
    }

    /** @return true while the device is detached by the watchdog. */
    bool isDetached() const { return _phase == Phase::Detached; }

    /** @return Consecutive attempts that delivered nothing (backoff level). */
    uint32_t failedAttempts() const { return _attemptsWithoutDelivery; }

    /** @return true when the host has read data from the dongle in this connection. */
    bool hostHasRead() const { return _hostReadThisMount; }

    UsbWatchdogAction tick(const UsbWatchdogInputs &in)
    {
        const uint32_t now = in.now;

        // A detach is always undone, whatever else is going on.
        if (_phase == Phase::Detached)
        {
            if (elapsed(now, _detachedAt) >= _cfg.detachMs)
            {
                _phase = Phase::Reattached;
                _reattachedAt = now;
                return UsbWatchdogAction::Attach;
            }
            return UsbWatchdogAction::None;
        }
        if (_phase == Phase::Reattached && elapsed(now, _reattachedAt) >= _cfg.recoveryWindowMs)
        {
            _phase = Phase::Normal;
        }

        const bool awake = in.mounted && !in.suspended;

        // Expire the host-active mark, so it can never look "recent" again
        // after a 32-bit millis() wrap.
        if (_hostRxValid && !hostRecentlyActive(now))
        {
            _hostRxValid = false;
        }

        // Delivery = the host read data we wrote: a write armed the endpoint and
        // it is free again. A bus reset (unmount, re-attach) also frees the
        // endpoint, so data in flight is forgotten then and never counts. Only
        // the real endpoint state can prove a delivery.
        if (!in.mounted)
        {
            _inFlight = false;
            _hostReadThisMount = false;
        }
        if (awake && in.wrote)
        {
            _inFlight = true;
        }
        if (awake && in.endpointKnown && _inFlight && !in.txBlocked)
        {
            _inFlight = false;
            onDelivered();
        }

        trackSuspend(in);
        trackBusy(in, awake);

        if (!in.mounted)
        {
            return UsbWatchdogAction::None;
        }
        if (in.suspended)
        {
            return tickSuspended(in);
        }
        return tickAwake(in);
    }

private:
    enum class Phase : uint8_t
    {
        Normal,
        Detached,
        Reattached,
    };

    UsbWatchdogMode _mode = UsbWatchdogMode::Recover;
    UsbWatchdogConfig _cfg;
    UsbHealthStats _stats;
    Phase _phase = Phase::Normal;

    uint32_t _detachedAt = 0;
    uint32_t _reattachedAt = 0;

    bool _hostRxValid = false;
    uint32_t _lastHostRxMs = 0;

    bool _inFlight = false;
    bool _hostReadThisMount = false;
    bool _busyValid = false;
    uint32_t _busySince = 0;
    bool _stallCounted = false;

    bool _suspendValid = false;
    uint32_t _suspendSince = 0;
    bool _wakeTried = false;
    uint32_t _lastWakeAt = 0;
    bool _wakeRefused = false;
    bool _reattachedThisSuspend = false;

    uint32_t _attemptsWithoutDelivery = 0;
    bool _gateValid = false;
    uint32_t _lastAttemptAt = 0;

    static uint32_t elapsed(uint32_t now, uint32_t since) { return now - since; }

    static uint32_t maxU32(uint32_t a, uint32_t b) { return a > b ? a : b; }

    void trackSuspend(const UsbWatchdogInputs &in)
    {
        if (in.mounted && in.suspended)
        {
            if (!_suspendValid)
            {
                _suspendValid = true;
                _suspendSince = in.now;
                _wakeTried = false;
                _wakeRefused = false;
                _reattachedThisSuspend = false;
                ++_stats.suspends;
            }
            _stats.longestSuspendMs = maxU32(_stats.longestSuspendMs, elapsed(in.now, _suspendSince));
        }
        else
        {
            _suspendValid = false;
        }
    }

    void trackBusy(const UsbWatchdogInputs &in, bool awake)
    {
        if (awake && in.txBlocked)
        {
            if (!_busyValid || in.wrote)
            {
                // Start (or restart on progress) the no-progress clock.
                _busyValid = true;
                _busySince = in.now;
                _stallCounted = false;
            }
            _stats.longestBusyMs = maxU32(_stats.longestBusyMs, elapsed(in.now, _busySince));
        }
        else
        {
            _busyValid = false;
        }
    }

    bool hostRecentlyActive(uint32_t now) const
    {
        if (!_hostRxValid)
        {
            return false;
        }
        // Signed: a timestamp taken slightly after `now` (later millis() in the
        // same loop) counts as active instead of wrapping to "long ago".
        return static_cast<int32_t>(now - _lastHostRxMs) < static_cast<int32_t>(_cfg.hostActiveGuardMs);
    }

    UsbWatchdogAction tickSuspended(const UsbWatchdogInputs &in)
    {
        // Data still sitting in the endpoint only counts when the host is known
        // to read: otherwise a host with no app listening would be re-attached
        // on every suspend. A message waiting in the queue always counts.
        const bool dataWaiting = in.pending || (_inFlight && in.txBlocked && _hostReadThisMount);
        if (!dataWaiting)
        {
            return UsbWatchdogAction::None;
        }
        const uint32_t now = in.now;
        if (!_wakeTried || (!_wakeRefused && elapsed(now, _lastWakeAt) >= _cfg.wakeRetryMs))
        {
            _wakeTried = true;
            _lastWakeAt = now;
            ++_stats.wakeupsTried;
            return UsbWatchdogAction::RemoteWakeup;
        }
        // An accepted wakeup is simply retried: the host may take seconds to
        // resume (e.g. waking from sleep) and must not be interrupted.
        if (_mode == UsbWatchdogMode::Off || !_cfg.recoverFromSuspend || !_wakeRefused ||
            _reattachedThisSuspend || !gateOpen(now))
        {
            return UsbWatchdogAction::None;
        }
        _reattachedThisSuspend = true;
        return startReattach(now);
    }

    UsbWatchdogAction tickAwake(const UsbWatchdogInputs &in)
    {
        const uint32_t now = in.now;
        if (!_busyValid || elapsed(now, _busySince) < _cfg.stallMs)
        {
            return UsbWatchdogAction::None;
        }
        if (!_stallCounted)
        {
            _stallCounted = true;
            ++_stats.stalls;
        }
        if (_mode == UsbWatchdogMode::Off || !in.endpointKnown || !_hostReadThisMount ||
            hostRecentlyActive(now) || !gateOpen(now))
        {
            return UsbWatchdogAction::None;
        }
        return startReattach(now);
    }

    bool gateOpen(uint32_t now) const
    {
        return !_gateValid || elapsed(now, _lastAttemptAt) >= backoffFor(_attemptsWithoutDelivery);
    }

    uint32_t backoffFor(uint32_t attempts) const
    {
        uint32_t delay = _cfg.backoffBaseMs;
        for (uint32_t i = 1; i < attempts && delay < _cfg.backoffMaxMs; ++i)
        {
            delay = (delay > _cfg.backoffMaxMs / 2) ? _cfg.backoffMaxMs : delay * 2;
        }
        return delay < _cfg.backoffMaxMs ? delay : _cfg.backoffMaxMs;
    }

    UsbWatchdogAction startReattach(uint32_t now)
    {
        ++_stats.reattaches;
        ++_attemptsWithoutDelivery;
        _gateValid = true;
        _lastAttemptAt = now;
        if (_mode != UsbWatchdogMode::Recover)
        {
            return UsbWatchdogAction::None;
        }
        _phase = Phase::Detached;
        _detachedAt = now;
        _inFlight = false;
        _busyValid = false;
        return UsbWatchdogAction::Detach;
    }

    void onDelivered()
    {
        _hostReadThisMount = true;
        if (_attemptsWithoutDelivery > 0)
        {
            ++_stats.recoveries;
        }
        _attemptsWithoutDelivery = 0;
        _gateValid = false;
        if (_phase == Phase::Reattached)
        {
            _phase = Phase::Normal;
        }
    }
};

/**
 * @brief Finds the IN (device-to-host) endpoint of the first USB MIDI streaming
 * interface in a configuration descriptor.
 *
 * @param cfg Full configuration descriptor (e.g. tud_descriptor_configuration_cb(0)).
 * @return Endpoint address (bit 7 set), or 0 when not found / malformed.
 */
inline uint8_t findMidiInEndpoint(const uint8_t *cfg)
{
    constexpr uint8_t kDescConfiguration = 0x02;
    constexpr uint8_t kDescInterface = 0x04;
    constexpr uint8_t kDescEndpoint = 0x05;
    constexpr uint8_t kClassAudio = 0x01;
    constexpr uint8_t kSubclassMidiStreaming = 0x03;

    if (!cfg || cfg[0] < 4 || cfg[1] != kDescConfiguration)
    {
        return 0;
    }
    const uint16_t total = static_cast<uint16_t>(cfg[2] | (cfg[3] << 8));
    uint16_t pos = 0;
    bool inMidiStreaming = false;
    while (pos + 2 <= total)
    {
        const uint8_t len = cfg[pos];
        const uint8_t type = cfg[pos + 1];
        if (len < 2 || pos + len > total)
        {
            break;
        }
        if (type == kDescInterface && len >= 9)
        {
            inMidiStreaming = cfg[pos + 5] == kClassAudio && cfg[pos + 6] == kSubclassMidiStreaming;
        }
        else if (type == kDescEndpoint && len >= 7 && inMidiStreaming && (cfg[pos + 2] & 0x80))
        {
            return cfg[pos + 2];
        }
        pos = static_cast<uint16_t>(pos + len);
    }
    return 0;
}

} // namespace enomik
