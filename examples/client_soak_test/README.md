# client_soak_test

Long-run test for the ESP-NOW send path. A client sends MIDI to the dongle for
hours and checks that the ESP-NOW driver keeps confirming its sends. It is made
to catch the hang where `esp_now_send()` refuses every send with
`ESP_ERR_ESPNOW_NO_MEM`, no confirmations come back and only a reboot helps.

## Setup

1. Dongle: any dongle firmware (for example `grantler_instruments/dongle`).
2. Client: set `dongleMac` in `client_soak_test.ino`, then flash:

   ```sh
   arduino-cli compile --upload -p /dev/cu.usbmodem1201 \
     --fqbn esp32:esp32:lolin_s2_mini examples/client_soak_test
   ```

   Change a setting without editing the sketch:

   ```sh
   arduino-cli compile --upload -p /dev/cu.usbmodem1201 \
     --fqbn esp32:esp32:lolin_s2_mini \
     --build-property "compiler.cpp.extra_flags=-DSOAK_PROFILE=0 -DSOAK_EXTRA_PEERS=2" \
     examples/client_soak_test
   ```

3. Log both boards to a file (Ctrl+C to stop, survives replugging):

   ```sh
   python3 examples/client_soak_test/soak_logger.py benchmarks/0.19.0/soak/soak.log \
     client=/dev/cu.usbmodem1201 dongle=/dev/cu.usbmodem1301
   ```

   The terminal shows the minute summary and events; the file has everything.
   Keep results under `benchmarks/<library version>/soak/`.

4. Optional, to load the other direction too: send a burst of 100 notes from
   the computer through the dongle to the client every 60 s; the client sends
   them back (`SOAK_ECHO_CHANNEL`). Needs `mido` and `python-rtmidi`, e.g. the
   wizard's venv:

   ```sh
   scripts/wizard/.venv/bin/python examples/client_soak_test/soak_bursts.py \
     benchmarks/0.19.0/soak/soak_bursts.log 60
   ```

   One line per burst (arrived at the client, came back, times), a summary per
   hour, totals on Ctrl+C. Optional third argument: hours to run. If opening
   the MIDI ports hangs (CoreMIDI), it gives up after 10 s; `killall MIDIServer`
   with no MIDI app open fixes that.

   Keep the computer awake, e.g. `caffeinate -i` in another terminal.

The client also runs without a computer (power bank): results are kept in flash
and printed after the next boot.

## Settings

| Define | Default | |
|---|---|---|
| `SOAK_PROFILE` | `SOAK_BURSTS` | `SOAK_LOW` (0): 7 messages 100 ms apart, 2 s pause, like the original hang sketch. `SOAK_HIGH` (1): about 350 messages/s. `SOAK_BURSTS` (2): low, with 30 s of high every 10 min. |
| `SOAK_EXTRA_PEERS` | 0 | Peers that do not exist (0..3), added on top of the dongle, so each message goes to several peers. Not stored. |
| `SOAK_KEEP_INPUTS` | 0 | 0: pins configured on the board read as idle, so only the sketch sends. 1: they send as usual (an unconnected analog pin floods). |
| `SOAK_ECHO_CHANNEL` | 10 | Note ons received on this channel with velocity 99 are sent back, to measure round trips through the dongle during the load. 0 = off. |

Serial commands: `p` print now, `c` clear saved results, `l` / `h` / `b`
switch profile, `?` help.

## Reading the results

Every minute:

```
[soak] boot=5 (software) up=0d02h10m00s profile=bursts extraPeers=0 attempts=... accepted=...
       sent=... failed=... delivered=... notDelivered=... throttled=... skipped=... dropped=...
       replaced=... queuePeak=... restarts=0 wifiRestarts=0
       lastError=... stalls=0 longestGap=10ms heap=... minHeap=...
[soak] rate=2/s savedStalls=0 firstStall=none
```

- `attempts` messages the sketch tried, `accepted` the ones the send function
  reported as OK (with the send queue: queued). `sent`/`delivered`/`notDelivered`
  are the library's counters (one per peer and packet). `dropped`: refused because
  the send queue was full; `replaced`: controller values that replaced a waiting
  one; `queuePeak`: most packets waiting at once. `throttled` only counts with
  `ESP_NOW_MIDI_SEND_QUEUE=0`. `skipped`: packets left out for peers
  marked unreachable (`p` lists the peers and which are unreachable).
- `stalls` / `savedStalls`: times nothing was confirmed for 3 s while sending
  (this boot / since the last `c`). The first one is saved as `[first-stall]`.
- `longestGap`: longest wait for a confirmation that did come.
- `restarts` / `wifiRestarts`: the library restarting ESP-NOW because it hung.
- `boot` and the reset reason: a `PANIC`, `WATCHDOG` or `BROWNOUT` reboot is a
  finding too. A checkpoint is saved every 10 min, so the last one before such a
  reboot is printed after it as `[checkpoint]`.

Passed: `stalls=0`, `restarts=0`, no unexpected reboot, `minHeap` steady.

LED: short blink every 2 s = healthy; fast blink = stalled right now; double
blink = healthy, but a stall was saved since the last `c`.

To check that the test catches the hang, build with
`-DESP_NOW_MIDI_MAX_IN_FLIGHT=8 -DSOAK_PROFILE=1`: on an ESP32-S2 it stalled
after about 10 s.
