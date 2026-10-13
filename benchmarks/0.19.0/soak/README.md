# 0.19.0 soak test

![Soak test setup](../../../docs/images/benchmark-soak.svg)

2026-10-12 23:51 to 2026-10-13 09:52, code of 0.19.0 before release (the
working tree committed as 79429a5). Setup and scripts:
[`examples/client_soak_test`](../../../examples/client_soak_test/).

- Client: `client_soak_test`, `SOAK_PROFILE` bursts, no extra peers. It had
  been up 1 h 32 min when logging started, 11 h 30 min at the end.
- Computer: `soak_bursts.py`, 100 notes every 60 s through the dongle to the
  client, which echoes them back. The first line (23:51) is an aborted start;
  the run is from 23:52.

Files:

- `soak.log`: `soak_logger.py`, serial output of client and dongle, one
  summary per minute
- `soak_bursts.log`: `soak_bursts.py`, one line per burst, one per hour, total

## Results

Client, last summary (whole uptime):

| attempts | delivered | dropped (queue full, `ESP_ERR_ESPNOW_NO_MEM`) | replaced | queue peak | stalls | restarts | longest gap | min heap |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 797,881 | 881,056 | 4,308 | 33,205 | 114 | 0 | 0 | 52 ms | 97,488 B |

`delivered` includes the echoed burst notes and heartbeats, so it is higher
than `attempts`.

Bursts (600, 100 notes each):

| Direction | Arrived | Avg | P95 | Max |
| --- | ---: | ---: | ---: | ---: |
| Computer to client | 60,000 / 60,000 | 135 ms | 258 ms | 320 ms |
| Client back to computer | 59,717 / 60,000 | 251 ms | 503 ms | 672 ms |

Times are for a whole burst of 100 notes queued at once, not single-message
latency. 51 bursts had echoes missing (283 notes, 0.5 %).

## Conclusion

The send path held up: no stalls or restarts in 10 h. Throughput is the
limit. Every MIDI message is its own ESP-NOW packet, and only one packet is
in flight at a time. So a burst of 100 notes took on average 135 ms to reach
the client, and 251 ms for the echo. The queue overflowed 4,308 times.

Packing several messages into one ESP-NOW packet could help. A packet carries
up to 250 bytes and a short MIDI message 1 to 3, so one confirmation round
trip could carry many messages instead of one. This needs more research:

- Packet format: today a packet longer than 3 bytes must be a SysEx frame
  (`include/esp_now_midi_sysex.h`), so batching needs a new, versioned frame
  type that older receivers ignore or reject.
- Latency: how long to wait to collect messages before sending, without
  delaying single notes.
- Loss: one lost packet then loses several messages, and there is no retry.
- Measure with this soak test (same profile and bursts) against these numbers.
