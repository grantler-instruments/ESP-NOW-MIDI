#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "include/UsbStallWatchdog.h"

using enomik::UsbStallWatchdog;
using enomik::UsbWatchdogAction;
using enomik::UsbWatchdogInputs;
using enomik::UsbWatchdogMode;

namespace {

using A = UsbWatchdogAction;

struct Sim
{
    UsbStallWatchdog wd;
    UsbWatchdogInputs in;
    bool detached = false;
    int detaches = 0;
    int attaches = 0;
    int wakeups = 0;
    bool wakeupAccepted = false;

    explicit Sim(bool hostListens = true)
    {
        in.mounted = true;
        in.endpointKnown = true;
        if (hostListens)
        {
            // The host reads one message: an app is known to listen.
            writeStuck();
            run(1);
            hostReads();
            run(1);
            REQUIRE(wd.hostHasRead());
        }
    }

    // Advance time in 1 ms loop steps, performing actions like the dongle does.
    A run(uint32_t ms)
    {
        A last = A::None;
        for (uint32_t i = 0; i < ms; ++i)
        {
            in.now++;
            last = step();
        }
        return last;
    }

    A step()
    {
        const A a = wd.tick(in);
        in.wrote = false;
        switch (a)
        {
        case A::Detach:
            REQUIRE_FALSE(detached);
            detached = true;
            detaches++;
            break;
        case A::Attach:
            REQUIRE(detached);
            detached = false;
            attaches++;
            break;
        case A::RemoteWakeup:
            wakeups++;
            wd.noteWakeupResult(wakeupAccepted);
            break;
        default:
            break;
        }
        return a;
    }

    // A message is handed to USB and the host does not read it.
    void writeStuck()
    {
        in.wrote = true;
        in.txBlocked = true;
    }

    // Host reads whatever was waiting.
    void hostReads()
    {
        in.txBlocked = false;
        in.pending = false;
    }
};

} // namespace

TEST_CASE("watchdog: idle bus never acts", "[dongle][watchdog]")
{
    Sim s;
    s.run(60000);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wakeups == 0);
    REQUIRE(s.wd.stats().stalls == 0);
}

TEST_CASE("watchdog: healthy host reading every message never acts", "[dongle][watchdog]")
{
    Sim s;
    for (int i = 0; i < 1000; ++i)
    {
        s.writeStuck();
        s.run(2); // host reads within a couple of ms
        s.hostReads();
        s.run(50);
    }
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().stalls == 0);
}

TEST_CASE("watchdog: heavy traffic with progress never trips", "[dongle][watchdog]")
{
    Sim s;
    // Endpoint sampled busy all the time, but new writes keep being accepted.
    s.in.txBlocked = true;
    s.in.pending = true;
    for (int i = 0; i < 5000; ++i)
    {
        s.in.wrote = true;
        s.run(10);
    }
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().stalls == 0);
}

TEST_CASE("watchdog: stuck endpoint re-attaches after 500 ms", "[dongle][watchdog]")
{
    Sim s;
    s.run(10);
    s.writeStuck();
    s.run(499);
    REQUIRE(s.detaches == 0);
    s.run(2);
    REQUIRE(s.detaches == 1);
    REQUIRE(s.detached);
    REQUIRE(s.wd.isRecovering(s.in.now));
    REQUIRE(s.wd.stats().stalls == 1);
    REQUIRE(s.wd.stats().reattaches == 1);
}

TEST_CASE("watchdog: attach always follows detach after 150 ms", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    s.run(501);
    REQUIRE(s.detached);

    // Whatever the inputs claim meanwhile, attach comes.
    s.in.mounted = false;
    s.in.suspended = true;
    s.run(149);
    REQUIRE(s.detached);
    s.run(1);
    REQUIRE_FALSE(s.detached);
    REQUIRE(s.attaches == 1);
}

TEST_CASE("watchdog: delivery after re-attach counts as recovery and re-arms", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    s.run(700); // detach + attach
    REQUIRE(s.attaches == 1);
    REQUIRE(s.wd.isRecovering(s.in.now));

    // Re-enumeration, then the queued message goes out and is read.
    s.in.txBlocked = false;
    s.in.mounted = false;
    s.run(300);
    s.in.mounted = true;
    s.writeStuck();
    s.run(1);
    s.hostReads();
    s.run(1);
    REQUIRE(s.wd.stats().recoveries == 1);
    REQUIRE(s.wd.failedAttempts() == 0);
    REQUIRE_FALSE(s.wd.isRecovering(s.in.now));

    // A later stall is handled immediately again (no backoff).
    s.run(1000);
    s.writeStuck();
    s.run(501);
    REQUIRE(s.detaches == 2);
}

TEST_CASE("watchdog: recovery window ends after 3 s without delivery", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    s.run(651);
    REQUIRE(s.attaches == 1);
    s.in.txBlocked = false;
    REQUIRE(s.wd.isRecovering(s.in.now));
    s.run(3000);
    REQUIRE_FALSE(s.wd.isRecovering(s.in.now));
}

TEST_CASE("watchdog: nobody reading backs off 30 s doubling to 10 min", "[dongle][watchdog]")
{
    Sim s;
    // Each message ends up stuck: no app reads the input.
    // A message arrives every 100 ms; it stays stuck until a re-attach
    // (the bus reset clears the endpoint).
    auto stuckFor = [&](uint32_t ms) {
        for (uint32_t t = 0; t < ms; t += 100)
        {
            if (!s.detached && !s.in.txBlocked)
            {
                s.writeStuck();
            }
            const int before = s.detaches;
            s.run(100);
            if (s.detaches != before)
            {
                s.in.txBlocked = false;
            }
        }
    };

    s.writeStuck();
    s.run(501);
    REQUIRE(s.detaches == 1);
    s.run(200);
    s.in.txBlocked = false;

    // Within 30 s no second attempt, even though it is stuck again.
    stuckFor(29000);
    REQUIRE(s.detaches == 1);
    stuckFor(2000);
    REQUIRE(s.detaches == 2);

    // Next gap is 60 s.
    stuckFor(58000);
    REQUIRE(s.detaches == 2);
    stuckFor(3000);
    REQUIRE(s.detaches == 3);

    // Over ~2 hours the gaps grow and cap at 10 min.
    stuckFor(2 * 60 * 60 * 1000);
    // 120+240+480 s, then every 600 s: 3 + ~10 more, never a loop.
    REQUIRE(s.detaches <= 16);
    REQUIRE(s.detaches >= 12);
    REQUIRE(s.wd.stats().recoveries == 0);
}

TEST_CASE("watchdog: host sending MIDI blocks stall recovery", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    for (int i = 0; i < 20; ++i)
    {
        s.wd.noteHostRx(s.in.now);
        s.run(100);
    }
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().stalls == 1);
    // Host goes quiet (last MIDI 100 ms ago): recovery once the 1 s guard ends.
    s.run(899);
    REQUIRE(s.detaches == 0);
    s.run(2);
    REQUIRE(s.detaches == 1);
}

TEST_CASE("watchdog: Observe counts but never detaches", "[dongle][watchdog]")
{
    Sim s;
    s.wd.setMode(UsbWatchdogMode::Observe);
    s.writeStuck();
    s.run(10000);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().stalls == 1);
    REQUIRE(s.wd.stats().reattaches == 1); // "would have", once, then backoff
    REQUIRE_FALSE(s.wd.isRecovering(s.in.now));
}

TEST_CASE("watchdog: Off never detaches", "[dongle][watchdog]")
{
    Sim s;
    s.wd.setMode(UsbWatchdogMode::Off);
    s.writeStuck();
    s.run(10000);
    s.in.suspended = true;
    s.in.pending = true;
    s.run(10000);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().reattaches == 0);
}

TEST_CASE("watchdog: suspend without data does nothing", "[dongle][watchdog]")
{
    Sim s;
    s.in.suspended = true;
    s.run(60000);
    REQUIRE(s.wakeups == 0);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().suspends == 1);
    REQUIRE(s.wd.stats().longestSuspendMs >= 59999);
}

TEST_CASE("watchdog: suspend with refused wakeup re-attaches immediately", "[dongle][watchdog]")
{
    Sim s;
    s.in.suspended = true;
    s.run(5000);
    s.wakeupAccepted = false;
    s.in.pending = true;
    s.run(1);
    REQUIRE(s.wakeups == 1);
    s.run(1);
    REQUIRE(s.detaches == 1);
    REQUIRE(s.wd.stats().wakeupsRefused == 1);
}

TEST_CASE("watchdog: accepted wakeup that resumes needs no re-attach", "[dongle][watchdog]")
{
    Sim s;
    s.in.suspended = true;
    s.in.pending = true;
    s.wakeupAccepted = true;
    s.run(1);
    REQUIRE(s.wakeups == 1);
    s.run(30);
    s.in.suspended = false; // host resumed the bus
    s.run(2000);
    REQUIRE(s.detaches == 0);
}

TEST_CASE("watchdog: accepted wakeup is retried, never interrupted by a re-attach", "[dongle][watchdog]")
{
    // E.g. a computer waking from sleep takes seconds to resume the bus.
    Sim s;
    s.in.suspended = true;
    s.in.pending = true;
    s.wakeupAccepted = true;
    s.run(5000);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wakeups == 5);
    s.in.suspended = false;
    s.run(10);
    REQUIRE(s.detaches == 0);
}

TEST_CASE("watchdog: at most one re-attach per suspend", "[dongle][watchdog]")
{
    Sim s;
    s.in.suspended = true;
    s.in.pending = true;
    s.run(2);
    REQUIRE(s.detaches == 1);
    s.run(200); // attach done, still suspended (e.g. whole PC asleep)
    s.run(60 * 60 * 1000);
    REQUIRE(s.detaches == 1);
}

TEST_CASE("watchdog: refused wakeup is not retried within a suspend", "[dongle][watchdog]")
{
    Sim s;
    s.wd.setMode(UsbWatchdogMode::Off);
    s.in.suspended = true;
    s.in.pending = true;
    s.run(10000);
    REQUIRE(s.wakeups == 1);
}

TEST_CASE("watchdog: accepted wakeup retried at most once per second", "[dongle][watchdog]")
{
    Sim s;
    s.wd.setMode(UsbWatchdogMode::Off);
    s.wakeupAccepted = true;
    s.in.suspended = true;
    s.in.pending = true;
    s.run(10000);
    REQUIRE(s.wakeups == 10);
}

TEST_CASE("watchdog: long suspend then resume does not trip the stall check", "[dongle][watchdog]")
{
    Sim s;
    s.wakeupAccepted = true;
    s.wd.setMode(UsbWatchdogMode::Off); // isolate the stall clock
    s.writeStuck();
    s.in.suspended = true;
    s.run(60000);
    s.in.suspended = false;
    s.run(5);
    s.hostReads();
    s.run(5000);
    REQUIRE(s.wd.stats().stalls == 0);
}

TEST_CASE("watchdog: not mounted never acts", "[dongle][watchdog]")
{
    Sim s;
    s.in.mounted = false;
    s.in.pending = true;
    s.in.txBlocked = true;
    s.run(60000);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wakeups == 0);
}

TEST_CASE("watchdog: survives millis() rollover", "[dongle][watchdog]")
{
    Sim s;
    s.in.now = 0xFFFFFF00u;
    s.writeStuck();
    s.run(400); // crosses zero
    REQUIRE(s.detaches == 0);
    s.run(101);
    REQUIRE(s.detaches == 1);
    s.run(200);
    REQUIRE(s.attaches == 1);
}

TEST_CASE("watchdog: longest busy is measured", "[dongle][watchdog]")
{
    Sim s;
    s.wd.setMode(UsbWatchdogMode::Off);
    s.writeStuck();
    s.run(120);
    s.hostReads();
    s.run(10);
    REQUIRE(s.wd.stats().longestBusyMs >= 119);
    REQUIRE(s.wd.stats().longestBusyMs <= 121);
    REQUIRE(s.wd.stats().stalls == 0);
}

TEST_CASE("watchdog: endpoint freed by the re-attach is not a delivery", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    s.run(501);
    REQUIRE(s.detaches == 1);
    s.run(150);
    REQUIRE(s.attaches == 1);
    // Bus reset clears the busy flag; the loop never saw mounted go false.
    s.in.txBlocked = false;
    s.run(100);
    REQUIRE(s.wd.stats().recoveries == 0);
    REQUIRE(s.wd.failedAttempts() == 1);
    // So a renewed stall waits for the backoff instead of looping.
    s.writeStuck();
    s.run(10000);
    REQUIRE(s.detaches == 1);
}

TEST_CASE("watchdog: delivery across a suspend counts", "[dongle][watchdog]")
{
    Sim s;
    s.wd.setMode(UsbWatchdogMode::Off);
    s.writeStuck();
    s.run(5);
    s.in.suspended = true;
    s.run(100);
    s.in.suspended = false;
    s.run(1);
    s.hostReads();
    s.run(1);
    REQUIRE(s.wd.stats().stalls == 0);
}

TEST_CASE("findMidiInEndpoint picks the MIDI streaming IN endpoint", "[dongle][watchdog]")
{
    // Config header + CDC (with its own IN endpoints) + Adafruit-style MIDI.
    const uint8_t cfg[] = {
        9, 0x02, 0, 0, 4, 1, 0, 0xA0, 50, // wTotalLength patched below
        // CDC control interface + notification IN endpoint 0x81
        9, 0x04, 0, 0, 1, 0x02, 0x02, 0x00, 0,
        7, 0x05, 0x81, 0x03, 8, 0, 16,
        // CDC data interface + OUT 0x02 / IN 0x82
        9, 0x04, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
        7, 0x05, 0x02, 0x02, 64, 0, 0,
        7, 0x05, 0x82, 0x02, 64, 0, 0,
        // Audio Control interface
        9, 0x04, 2, 0, 0, 0x01, 0x01, 0x00, 0,
        9, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 1, 3,
        // MIDI Streaming interface
        9, 0x04, 3, 0, 2, 0x01, 0x03, 0x00, 0,
        7, 0x24, 0x01, 0x00, 0x01, 0x41, 0x00,
        // OUT endpoint 0x03 + CS endpoint
        9, 0x05, 0x03, 0x02, 64, 0, 0, 0, 0,
        5, 0x25, 0x01, 1, 1,
        // IN endpoint 0x84 + CS endpoint
        9, 0x05, 0x84, 0x02, 64, 0, 0, 0, 0,
        5, 0x25, 0x01, 1, 3,
    };
    uint8_t buf[sizeof(cfg)];
    memcpy(buf, cfg, sizeof(cfg));
    buf[2] = static_cast<uint8_t>(sizeof(cfg) & 0xFF);
    buf[3] = static_cast<uint8_t>(sizeof(cfg) >> 8);
    REQUIRE(enomik::findMidiInEndpoint(buf) == 0x84);

    // No MIDI interface -> 0.
    buf[2] = 9 + 9 + 7 + 9 + 7 + 7; // only header + CDC
    REQUIRE(enomik::findMidiInEndpoint(buf) == 0);

    // Malformed lengths never read out of bounds and return 0.
    uint8_t bad[] = {9, 0x02, 20, 0, 1, 1, 0, 0xA0, 50, 0, 0x04};
    REQUIRE(enomik::findMidiInEndpoint(bad) == 0);
    REQUIRE(enomik::findMidiInEndpoint(nullptr) == 0);
}

TEST_CASE("watchdog: host that never read (no app listening) is left alone", "[dongle][watchdog]")
{
    Sim s(false);
    s.writeStuck();
    s.run(60 * 60 * 1000);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().stalls == 1);
}

TEST_CASE("watchdog: after a re-attach nobody reads -> no further stall recovery", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    s.run(501);
    REQUIRE(s.detaches == 1);
    s.run(150);
    // Bus reset during re-enumeration.
    s.in.txBlocked = false;
    s.in.mounted = false;
    s.run(200);
    s.in.mounted = true;
    REQUIRE_FALSE(s.wd.hostHasRead());
    // App closed meanwhile: messages pile up, forever.
    s.writeStuck();
    s.run(2 * 60 * 60 * 1000);
    REQUIRE(s.detaches == 1);
}

TEST_CASE("watchdog: fallback without endpoint state never recovers a stall", "[dongle][watchdog]")
{
    Sim s;
    s.in.endpointKnown = false;
    // Accepted writes must not count as deliveries in fallback mode.
    for (int i = 0; i < 100; ++i)
    {
        s.in.wrote = true;
        s.in.txBlocked = false;
        s.run(10);
    }
    s.in.txBlocked = true;
    s.in.pending = true;
    s.run(60000);
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wd.stats().recoveries == 0);
}

TEST_CASE("watchdog: host MIDI stamped after the loop's now still counts as active", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    for (int i = 0; i < 2000; ++i)
    {
        s.wd.noteHostRx(s.in.now + 1); // millis() ticked during the loop
        s.run(1);
    }
    REQUIRE(s.detaches == 0);
}

TEST_CASE("watchdog: backoff gate still opens after 25+ days", "[dongle][watchdog]")
{
    Sim s;
    s.writeStuck();
    s.run(501);
    REQUIRE(s.detaches == 1);
    s.run(150);
    s.in.txBlocked = false;
    s.run(10);
    // 25 days later, a real stall (host had read again meanwhile).
    s.in.now += 25u * 24u * 60u * 60u * 1000u;
    s.writeStuck();
    s.run(1);
    s.hostReads();
    s.run(1);
    s.writeStuck();
    s.run(501);
    REQUIRE(s.detaches == 2);
}

TEST_CASE("watchdog: recoverFromSuspend=false never re-attaches on suspend", "[dongle][watchdog]")
{
    Sim s;
    s.wd.config().recoverFromSuspend = false;
    s.in.suspended = true;
    s.in.pending = true;
    s.run(60000);
    REQUIRE(s.wakeups == 1);
    REQUIRE(s.detaches == 0);
}

TEST_CASE("watchdog: suspend with an idle armed endpoint and nothing waiting does nothing", "[dongle][watchdog]")
{
    Sim s;
    s.in.txBlocked = true; // armed endpoint, but nothing we wrote is in flight
    s.in.suspended = true;
    s.run(60000);
    REQUIRE(s.wakeups == 0);
    REQUIRE(s.detaches == 0);
}

TEST_CASE("watchdog: host-active mark expires and cannot revive after 25 days", "[dongle][watchdog]")
{
    Sim s;
    s.wd.noteHostRx(s.in.now);
    s.run(2000);
    s.in.now += 25u * 24u * 60u * 60u * 1000u; // signed elapsed would be negative now
    s.writeStuck();
    s.run(1);
    s.hostReads();
    s.run(1);
    s.writeStuck();
    s.run(501);
    REQUIRE(s.detaches == 1);
}

TEST_CASE("watchdog: no app listening + repeated suspends does not re-attach", "[dongle][watchdog]")
{
    Sim s(false); // host never read
    s.writeStuck(); // stays armed in the endpoint, queue empty
    for (int i = 0; i < 20; ++i)
    {
        s.in.suspended = true;
        s.run(60000);
        s.in.suspended = false;
        s.run(60000);
    }
    REQUIRE(s.detaches == 0);
    REQUIRE(s.wakeups == 0);
}

TEST_CASE("watchdog: button press queued during suspend still recovers without prior reads", "[dongle][watchdog]")
{
    Sim s(false); // e.g. Windows suspended the dongle before anything was sent
    s.in.suspended = true;
    s.run(10000);
    s.in.pending = true; // the press waits in the queue (not written while suspended)
    s.run(2);
    REQUIRE(s.wakeups == 1);
    REQUIRE(s.detaches == 1);
}
