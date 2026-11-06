"""Logs the serial output of the soak test boards to a file, with timestamps.

    python3 soak_logger.py soak.log client=/dev/cu.usbmodem1201 dongle=/dev/cu.usbmodem1301

Runs until Ctrl+C. Reopens a port after the board was replugged or rebooted.
Lines with STALL, recovered, restarted or a reset reason other than power-on
are also printed to the terminal.
"""
import fcntl
import os
import select
import struct
import sys
import termios
import time

TIOCMBIS = 0x8004746C  # macOS: set modem bits
TIOCM_DTR = 0x002
HIGHLIGHT = ("STALL", "recovered", "restarted", "PANIC", "WATCHDOG", "BROWNOUT", "port ")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    out = open(sys.argv[1], "a", buffering=1)
    ports = dict(arg.split("=", 1) for arg in sys.argv[2:])
    fds, buf = {}, {}

    def log(name, text):
        line = f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {name}: {text}"
        out.write(line + "\n")
        if any(word in text for word in HIGHLIGHT) or text.startswith("[soak] rate"):
            print(line, flush=True)

    while True:
        for name, port in ports.items():
            if name not in fds and os.path.exists(port):
                try:
                    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
                    attrs = termios.tcgetattr(fd)
                    attrs[3] &= ~(termios.ICANON | termios.ECHO)
                    termios.tcsetattr(fd, termios.TCSANOW, attrs)
                    fcntl.ioctl(fd, TIOCMBIS, struct.pack("I", TIOCM_DTR))
                    fds[name], buf[name] = fd, ""
                    log(name, "--- port opened ---")
                except OSError as e:
                    log(name, f"open failed: {e}")
        if not fds:
            time.sleep(0.5)
            continue
        ready, _, _ = select.select(list(fds.values()), [], [], 0.5)
        for name, fd in list(fds.items()):
            if fd not in ready:
                continue
            try:
                data = os.read(fd, 4096)
            except OSError:
                data = b""
            if not data:
                os.close(fd)
                del fds[name]
                log(name, "--- port closed ---")
                continue
            buf[name] += data.decode(errors="replace")
            *lines, buf[name] = buf[name].split("\n")
            for line in lines:
                log(name, line.rstrip("\r"))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
