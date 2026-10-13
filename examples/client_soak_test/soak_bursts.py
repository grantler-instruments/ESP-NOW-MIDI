#!/usr/bin/env python3
"""Sends a burst of notes through the dongle to the soak client, during a soak run.

Every INTERVAL seconds: 100 note ons (channel 10, velocity 99) at once into
the dongle. The client (client_soak_test, SOAK_ECHO_CHANNEL 10) sends each one
back. Logs how many reached the client (its USB port) and came back (the
dongle's port), with times. One line per burst, a summary per hour, totals
at the end (Ctrl-C).

Usage: python3 soak_bursts.py soak_bursts.log [interval_s=60] [hours=0 (until Ctrl-C)]
Needs mido and python-rtmidi (e.g. scripts/wizard/.venv).
"""
import os
import statistics
import sys
import threading
import time

import mido

DONGLE_PORT = "enomik3000_dongle"
CLIENT_PORT = "enomik3000_client"
NOTES = 100
CHANNEL = 9  # channel 10
VELOCITY = 99
WAIT_S = 3.0  # per burst, for the notes to come back

out = open(sys.argv[1] if len(sys.argv) > 1 else "soak_bursts.log", "a", buffering=1)
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 60.0
hours = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0


def log(text):
    line = f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {text}"
    print(line, flush=True)
    out.write(line + "\n")


def find(names, wanted):
    for name in names:
        if name.startswith(wanted):
            return name
    return None


# Opening a CoreMIDI port sometimes hangs for good; do not wait all night.
def give_up():
    log("ERROR: opening the MIDI ports hangs (CoreMIDI). Run 'killall MIDIServer' "
        "(no MIDI app open) and start again.")
    os._exit(2)


timer = threading.Timer(10, give_up)
timer.start()
dongle_name = find(mido.get_input_names(), DONGLE_PORT)
client_name = find(mido.get_input_names(), CLIENT_PORT)
if not dongle_name or not client_name:
    timer.cancel()
    log(f"ERROR: MIDI ports not found (dongle: {dongle_name}, client: {client_name})")
    os._exit(2)
d_in = mido.open_input(dongle_name)
d_out = mido.open_output(find(mido.get_output_names(), DONGLE_PORT))
c_in = mido.open_input(client_name)
timer.cancel()
log(f"started: burst of {NOTES} every {interval:.0f} s"
    + (f" for {hours:g} h" if hours else " until Ctrl-C"))

total = {"bursts": 0, "sent": 0, "there": 0, "back": 0, "one_way": [], "rtt": []}
hour = {"bursts": 0, "sent": 0, "there": 0, "back": 0, "one_way": [], "rtt": []}
other_from_dongle = 0


def drain(seconds):
    global other_from_dongle
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        for _ in c_in.iter_pending():
            pass
        for _ in d_in.iter_pending():
            other_from_dongle += 1
        time.sleep(0.005)


def burst():
    global other_from_dongle
    sent, there, back = {}, {}, {}
    for note in range(NOTES):
        sent[note] = time.monotonic()
        d_out.send(mido.Message("note_on", channel=CHANNEL, note=note, velocity=VELOCITY))
    end = time.monotonic() + WAIT_S
    while (len(there) < NOTES or len(back) < NOTES) and time.monotonic() < end:
        now = time.monotonic()
        for m in c_in.iter_pending():
            if (m.type == "note_on" and m.channel == CHANNEL and m.velocity == VELOCITY
                    and m.note in sent and m.note not in there):
                there[m.note] = now
        for m in d_in.iter_pending():
            if (m.type == "note_on" and m.channel == CHANNEL and m.velocity == VELOCITY
                    and m.note in sent and m.note not in back):
                back[m.note] = now
            else:
                other_from_dongle += 1
        time.sleep(0.0005)
    for note in range(NOTES):
        d_out.send(mido.Message("note_off", channel=CHANNEL, note=note, velocity=0))
    one_way = [(there[n] - sent[n]) * 1000 for n in there]
    rtt = [(back[n] - sent[n]) * 1000 for n in back]
    return one_way, rtt


def fmt(values):
    if not values:
        return "-"
    s = sorted(values)
    p95 = s[min(len(s) - 1, int(len(s) * 0.95))]
    return f"avg {statistics.mean(s):.0f} p95 {p95:.0f} max {s[-1]:.0f} ms"


def summary(label, stats):
    log(f"{label}: bursts={stats['bursts']} to client {stats['there']}/{stats['sent']} "
        f"({fmt(stats['one_way'])}), back {stats['back']}/{stats['sent']} ({fmt(stats['rtt'])})")


start = time.monotonic()
hour_end = start + 3600
try:
    while not hours or time.monotonic() - start < hours * 3600:
        burst_start = time.monotonic()
        one_way, rtt = burst()
        for stats in (total, hour):
            stats["bursts"] += 1
            stats["sent"] += NOTES
            stats["there"] += len(one_way)
            stats["back"] += len(rtt)
            stats["one_way"] += one_way
            stats["rtt"] += rtt
        flag = "" if len(rtt) == NOTES and len(one_way) == NOTES else "  <-- missing"
        log(f"burst {total['bursts']}: to client {len(one_way)}/{NOTES} ({fmt(one_way)}), "
            f"back {len(rtt)}/{NOTES} ({fmt(rtt)}), other from dongle {other_from_dongle}{flag}")
        other_from_dongle = 0
        if time.monotonic() >= hour_end:
            summary("hour", hour)
            hour = {"bursts": 0, "sent": 0, "there": 0, "back": 0, "one_way": [], "rtt": []}
            hour_end += 3600
        drain(max(0.0, interval - (time.monotonic() - burst_start)))
except KeyboardInterrupt:
    pass
summary("TOTAL", total)
out.flush()
os._exit(0)  # closing CoreMIDI ports at exit can hang
