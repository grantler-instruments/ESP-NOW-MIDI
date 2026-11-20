#!/usr/bin/env python3
"""Upload Pd patch files to an enomik_pd client over MIDI (MIDI File Dump).

Works through the client's own USB MIDI port, or through an enomik dongle
(the dongle forwards the SysEx over ESP-NOW to its clients).

    pip install python-rtmidi
    python3 pd_upload.py --list
    python3 pd_upload.py --port enomik pd/*.pd          # main.pd last: it triggers the reload
    python3 pd_upload.py --port dongle --device 3 synth.pd main.pd

Files are sent in the given order, but main.pd (or --main) is always moved to
the end, because receiving it makes the client reload the patch.
"""
import argparse
import os
import sys
import time

try:
    import rtmidi
except ImportError:
    sys.exit("needs python-rtmidi: pip install python-rtmidi")

UNIVERSAL_NON_RT = 0x7E
FILE_DUMP = 0x07
HEADER, DATA = 0x01, 0x02
EOF, WAIT, CANCEL, NAK, ACK = 0x7B, 0x7C, 0x7D, 0x7E, 0x7F
RAW_PER_PACKET = 112  # 16 groups of 7 bytes -> 128 encoded bytes (File Dump maximum)


def encode_7in8(raw: bytes) -> list:
    """Each group of up to 7 bytes becomes 1 MSB byte + 7 low-7-bit bytes."""
    out = []
    for i in range(0, len(raw), 7):
        group = raw[i:i + 7]
        msbs = 0
        for j, b in enumerate(group):
            msbs |= ((b >> 7) & 1) << (6 - j)
        out.append(msbs)
        out.extend(b & 0x7F for b in group)
    return out


# File Dump file types (MMA): 4 ASCII characters.
TEXT_EXTENSIONS = {".pd", ".txt", ".json", ".cfg", ".md"}


def file_type(name: str) -> bytes:
    """'TEXT' for patches and other text, 'MIDI' for standard MIDI files, 'BIN ' otherwise."""
    ext = os.path.splitext(name)[1].lower()
    if ext in TEXT_EXTENSIONS:
        return b"TEXT"
    if ext in (".mid", ".midi"):
        return b"MIDI"
    return b"BIN "


def header_msg(device: int, name: str, size: int) -> list:
    length = [(size >> s) & 0x7F for s in (0, 7, 14, 21)]
    body = [UNIVERSAL_NON_RT, device, FILE_DUMP, HEADER, 0x00] + list(file_type(name)) + length + list(name.encode("ascii"))
    return [0xF0] + body + [0xF7]


def data_msg(device: int, packet: int, raw: bytes) -> list:
    enc = encode_7in8(raw)
    body = [UNIVERSAL_NON_RT, device, FILE_DUMP, DATA, packet & 0x7F, len(enc) - 1] + enc
    checksum = 0
    for b in body:
        checksum ^= b
    return [0xF0] + body + [checksum & 0x7F, 0xF7]


class Link:
    def __init__(self, port_substr: str, timeout: float):
        self.out = rtmidi.MidiOut()
        self.inp = rtmidi.MidiIn()
        self.inp.ignore_types(sysex=False, timing=True, active_sense=True)
        self.timeout = timeout
        self._open(self.out, port_substr, "output")
        self._open(self.inp, port_substr, "input")

    @staticmethod
    def _open(dev, substr, kind):
        names = dev.get_ports()
        for i, n in enumerate(names):
            if substr.lower() in n.lower():
                dev.open_port(i)
                print(f"{kind}: {n}")
                return
        sys.exit(f"no MIDI {kind} matching '{substr}'. Available: {names}")

    def send(self, msg):
        self.out.send_message(msg)

    def wait_handshake(self, device: int):
        """Returns (subId, packet) of the next File Dump handshake, or None on timeout."""
        end = time.time() + self.timeout
        while time.time() < end:
            m = self.inp.get_message()
            if not m:
                time.sleep(0.001)
                continue
            d = m[0]
            if len(d) == 6 and d[0] == 0xF0 and d[1] == UNIVERSAL_NON_RT and d[3] in (WAIT, CANCEL, NAK, ACK):
                if device != 0x7F and d[2] != device:
                    continue  # another client's answer
                if d[3] == WAIT:
                    end = time.time() + self.timeout
                    continue
                return d[3], d[4]
        return None


def send_file(link: Link, device: int, path: str, retries: int = 5):
    name = os.path.basename(path)
    data = open(path, "rb").read()

    def exchange(msg, what):
        for _ in range(retries):
            link.send(msg)
            hs = link.wait_handshake(device)
            if hs is None:
                print(f"  {what}: no answer, retrying")
                continue
            sub, _pkt = hs
            if sub == ACK:
                return
            if sub == CANCEL:
                sys.exit(f"  {what}: cancelled by client")
            print(f"  {what}: NAK, retrying")
        sys.exit(f"  {what}: giving up")

    print(f"{name}: {len(data)} bytes")
    exchange(header_msg(device, name, len(data)), "header")
    packets = [data[i:i + RAW_PER_PACKET] for i in range(0, len(data), RAW_PER_PACKET)]
    for n, chunk in enumerate(packets):
        exchange(data_msg(device, n, chunk), f"packet {n}")
    exchange([0xF0, UNIVERSAL_NON_RT, device, EOF, len(packets) & 0x7F, 0xF7], "end of file")
    print("  stored")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--port", default="enomik", help="substring of the MIDI port name (default: enomik)")
    ap.add_argument("--device", type=int, default=0x7F, help="client device ID 0-126, 127 = all (default)")
    ap.add_argument("--main", default="main.pd", help="file that triggers the reload (sent last)")
    ap.add_argument("--timeout", type=float, default=2.0, help="seconds to wait for each answer")
    ap.add_argument("--list", action="store_true", help="list MIDI ports")
    args = ap.parse_args()

    if args.list:
        print("outputs:", rtmidi.MidiOut().get_ports())
        print("inputs: ", rtmidi.MidiIn().get_ports())
        return
    if not args.files:
        ap.error("no files")

    files = sorted(args.files, key=lambda f: os.path.basename(f) == args.main)
    link = Link(args.port, args.timeout)
    for f in files:
        send_file(link, args.device, f)


if __name__ == "__main__":
    main()
