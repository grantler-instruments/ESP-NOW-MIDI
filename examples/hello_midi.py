"""Hello MIDI for CircuitPython.

Copy esp_now_midi.py to CIRCUITPY/lib and this file to CIRCUITPY/code.py,
then set the dongle's MAC. Sends a note, a controller and pitch bend every
2 seconds and prints MIDI it receives.
"""

import time
from esp_now_midi import ESPNowMidi

peer_mac = b'\x84\xF7\x03\xF2\x54\x62'  # Dongle MAC
channel = 1

midi = ESPNowMidi()
midi.begin()
midi.add_peer(peer_mac)
midi.send_announce("hello_midi")  # name and online status for the dongle

midi.set_handle_note_on(lambda ch, note, vel: print("note on", ch, note, vel))
midi.set_handle_note_off(lambda ch, note, vel: print("note off", ch, note, vel))
midi.set_handle_control_change(lambda ch, cc, val: print("cc", ch, cc, val))

midi.print_peers()

steps = [
    lambda: midi.send_note_on(60, 127, channel),
    lambda: midi.send_note_off(60, 0, channel),
    lambda: midi.send_control_change(1, 127, channel),
    lambda: midi.send_control_change(1, 0, channel),
    lambda: midi.send_pitch_bend(-8192, channel),
    lambda: midi.send_pitch_bend(0, channel),
    lambda: midi.send_pitch_bend(8191, channel),
    lambda: midi.send_pitch_bend(0, channel),
]
step = 0
next_send = time.monotonic()

# Send on a timer instead of sleeping, so received MIDI is handled right away
while True:
    midi.read()
    now = time.monotonic()
    if now >= next_send:
        if not steps[step]():
            print("send failed")
        step = (step + 1) % len(steps)
        next_send = now + (2.0 if step == 0 else 0.1)
