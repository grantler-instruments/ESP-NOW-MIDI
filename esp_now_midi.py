"""
ESP-NOW MIDI Library for CircuitPython (ESP32 boards)
Handles MIDI message transmission over ESP-NOW protocol
Based on the C++ esp_now_midi library - packet format compatible

Uses CircuitPython's `espnow` module (espnow.Peer, e.peers, e.send(msg, peer),
e.read()) and `wifi` module. MicroPython is not supported.
"""

import espnow
from time import monotonic_ns as _monotonic_ns


def _now_ms():
    return _monotonic_ns() // 1000000

# Keep in sync with version.h (timed/jitter buffer not ported to CP yet)
VERSION_MAJOR = 1
VERSION_MINOR = 0
VERSION_PATCH = 0
VERSION = f"{VERSION_MAJOR}.{VERSION_MINOR}.{VERSION_PATCH}"


def get_version():
    """Return library version string (matches C++ getVersion())."""
    return VERSION

# MIDI Status Bytes
MIDI_UNKNOWN = 0x00
# Channel voice messages
MIDI_NOTE_OFF = 0x80
MIDI_NOTE_ON = 0x90
MIDI_POLY_AFTERTOUCH = 0xA0
MIDI_CONTROL_CHANGE = 0xB0
MIDI_PROGRAM_CHANGE = 0xC0
MIDI_AFTERTOUCH = 0xD0
MIDI_PITCH_BEND = 0xE0
# System messages
MIDI_SYSEX = 0xF0
MIDI_TIME_CODE = 0xF1
MIDI_SONG_POS_POINTER = 0xF2
MIDI_SONG_SELECT = 0xF3
MIDI_TUNE_REQUEST = 0xF6
SYSEX_END = 0xF7
MIDI_TIME_CLOCK = 0xF8
MIDI_START = 0xFA
MIDI_CONTINUE = 0xFB
MIDI_STOP = 0xFC
MIDI_ACTIVE_SENSING = 0xFE
MIDI_SYSTEM_RESET = 0xFF

# Constants
MIDI_MIN_BEND = 0
MIDI_MAX_BEND = 16383
MAX_PEERS = 20
ESP_NOW_MIDI_CHANNEL = 6

# SysEx transport (matches include/esp_now_midi_sysex.h)
SYSEX_MARKER = 0xF0
SYSEX_VERSION = 1
SYSEX_VERSION_SHIFT = 4
SYSEX_FLAG_FIRST = 0x01
SYSEX_FLAG_LAST = 0x02
SYSEX_HEADER_SIZE = 6
SYSEX_MAX_PAYLOAD = 240
SYSEX_MAX_MESSAGE = 1024
SYSEX_MAX_FRAME = SYSEX_HEADER_SIZE + SYSEX_MAX_PAYLOAD
SYSEX_REASSEMBLY_TIMEOUT_MS = 100

# Enomik SysEx ANNOUNCE (docs/enomik-sysex-protocol.md)
ENOMIK_MANUFACTURER_ID = 0x7D
ENOMIK_CMD_ANNOUNCE = 0x15
DEVICE_KIND_CLIENT = 0x00
DEVICE_KIND_DONGLE = 0x01
ANNOUNCE_MAX_NAME = 16


def _sysex_fragment_count(msg_len):
    if msg_len <= 0:
        return 0
    return (msg_len + SYSEX_MAX_PAYLOAD - 1) // SYSEX_MAX_PAYLOAD


def _sysex_fragment_payload_length(msg_len, seq):
    offset = seq * SYSEX_MAX_PAYLOAD
    if offset >= msg_len:
        return 0
    remaining = msg_len - offset
    return SYSEX_MAX_PAYLOAD if remaining > SYSEX_MAX_PAYLOAD else remaining


def _sysex_make_flags(first, last):
    flags = SYSEX_VERSION << SYSEX_VERSION_SHIFT
    if first:
        flags |= SYSEX_FLAG_FIRST
    if last:
        flags |= SYSEX_FLAG_LAST
    return flags


def _sysex_encode_frame(seq, total, msg_len, payload):
    payload_len = len(payload)
    if payload_len == 0 or payload_len > SYSEX_MAX_PAYLOAD:
        return None
    if msg_len == 0 or msg_len > SYSEX_MAX_MESSAGE or total < 1 or seq >= total:
        return None
    if _sysex_fragment_count(msg_len) != total:
        return None
    if payload_len != _sysex_fragment_payload_length(msg_len, seq):
        return None
    frame = bytearray(SYSEX_HEADER_SIZE + payload_len)
    frame[0] = SYSEX_MARKER
    frame[1] = _sysex_make_flags(seq == 0, seq == total - 1)
    frame[2] = seq
    frame[3] = total
    frame[4] = msg_len & 0xFF
    frame[5] = (msg_len >> 8) & 0xFF
    frame[SYSEX_HEADER_SIZE:] = payload
    return bytes(frame)


def _sysex_parse_frame(data):
    if data is None or len(data) < SYSEX_HEADER_SIZE + 1 or len(data) > SYSEX_MAX_FRAME:
        return None
    if data[0] != SYSEX_MARKER:
        return None
    flags = data[1]
    if ((flags >> SYSEX_VERSION_SHIFT) & 0x0F) != SYSEX_VERSION:
        return None
    seq = data[2]
    total = data[3]
    msg_len = data[4] | (data[5] << 8)
    payload = data[SYSEX_HEADER_SIZE:]
    payload_len = len(payload)
    if total < 1 or seq >= total:
        return None
    if msg_len == 0 or msg_len > SYSEX_MAX_MESSAGE:
        return None
    if _sysex_fragment_count(msg_len) != total:
        return None
    first = (flags & SYSEX_FLAG_FIRST) != 0
    last = (flags & SYSEX_FLAG_LAST) != 0
    if first != (seq == 0) or last != (seq == total - 1):
        return None
    if payload_len == 0 or payload_len != _sysex_fragment_payload_length(msg_len, seq):
        return None
    return {
        "flags": flags,
        "seq": seq,
        "total": total,
        "msg_len": msg_len,
        "payload": bytes(payload),
        "last": last,
    }


# System status bytes that are not a complete message, or undefined
_INVALID_SHORT_STATUS = (MIDI_SYSEX, SYSEX_END, 0xF4, 0xF5, 0xF9, 0xFD)


def _midi_data_size(status_byte):
    """Bytes in a short MIDI message with this status byte (1-3)."""
    if status_byte >= 0xF0:
        if status_byte in (MIDI_TIME_CODE, MIDI_SONG_SELECT):
            return 2
        if status_byte == MIDI_SONG_POS_POINTER:
            return 3
        if status_byte in (MIDI_TUNE_REQUEST, MIDI_TIME_CLOCK, MIDI_START, MIDI_CONTINUE,
                           MIDI_STOP, MIDI_ACTIVE_SENSING, MIDI_SYSTEM_RESET):
            return 1
        return 3
    if (status_byte & 0xF0) in (MIDI_PROGRAM_CHANGE, MIDI_AFTERTOUCH):
        return 2
    return 3


def is_midi_packet(data):
    """
    True when an ESP-NOW payload is a packet this library sends.
    Matches C++ isMidiPacket(): a 1-3 byte MIDI message whose length matches
    its status byte, or a SysEx transport frame.
    """
    if not data:
        return False
    if len(data) > 3:
        return _sysex_parse_frame(data) is not None
    status_byte = data[0]
    if status_byte < 0x80 or status_byte in _INVALID_SHORT_STATUS:
        return False
    if _midi_data_size(status_byte) != len(data):
        return False
    for b in data[1:]:
        if b & 0x80:
            return False
    return True


class ESPNowMidi:
    """
    ESP-NOW MIDI communication class

    Packet format matches C++ midi_message_packet: 1-3 bytes, status byte
    (status | 0-based channel) followed by the data bytes. Channels are 1-16 in
    this API. SysEx goes out as fragmented transport frames.
    """

    def __init__(self):
        self.e = None
        self.peers = []
        self._max_peers = MAX_PEERS
        self._auto_peer_discovery = True
        self._peer_objects = {}  # mac -> espnow.Peer
        self._channel = ESP_NOW_MIDI_CHANNEL
        self._last_seen = {}  # mac -> ms of the last valid packet
        self._last_sender_mac = None
        self._last_send_ms = None

        # MIDI Handlers (matching C++ private members)
        self._on_message_handler = None
        self._on_note_on_handler = None
        self._on_note_off_handler = None
        self._on_control_change_handler = None
        self._on_program_change_handler = None
        self._on_pitch_bend_handler = None
        self._on_after_touch_channel_handler = None
        self._on_after_touch_poly_handler = None
        self._on_start_handler = None
        self._on_stop_handler = None
        self._on_continue_handler = None
        self._on_clock_handler = None
        self._on_song_position_handler = None
        self._on_song_select_handler = None
        self._on_time_code_handler = None
        self._on_active_sensing_handler = None
        self._on_system_reset_handler = None
        self._on_sysex_handler = None
        self._on_sysex_from_handler = None
        # Single-sender reassembly
        self._sysex_assembly = None

    def begin(self, reduce_power_at_cost_of_latency=False, auto_peer_discovery=True, channel=ESP_NOW_MIDI_CHANNEL):
        """
        Initialize ESP-NOW. Calling it again starts over with no peers.

        Args:
            reduce_power_at_cost_of_latency: Not implemented in Python
            auto_peer_discovery: Add senders of valid MIDI packets as peers
            channel: WiFi channel for ESP-NOW (default matches C++ ESP_NOW_MIDI_CHANNEL)

        Returns:
            True when ESP-NOW is ready.
        """
        if self.e is not None:
            self.deinit()
        self._auto_peer_discovery = auto_peer_discovery
        self._channel = channel
        self._configure_wifi_channel(channel)
        try:
            self.e = espnow.ESPNow()
        except Exception as exc:
            print("ESP-NOW MIDI init failed:", exc)
            self.e = None
            return False
        print("ESP-NOW MIDI initialized")
        return True

    def _configure_wifi_channel(self, channel):
        """
        Put the radio on the ESP-NOW channel before ESP-NOW init.

        CircuitPython has no channel setter. Starting an access point on the
        channel and stopping it right away leaves the radio there (the approach
        Adafruit's ESP-NOW guide uses). When connected to a WiFi network the
        radio has to stay on that network's channel, so it is left alone.
        """
        try:
            import wifi
        except ImportError:
            return
        try:
            wifi.radio.enabled = True
            if wifi.radio.connected:
                ap = wifi.radio.ap_info
                if ap is not None and ap.channel != channel:
                    print("ESP-NOW MIDI: WiFi is connected on channel", ap.channel,
                          "- peers on channel", channel, "will not hear this device")
                return
            wifi.radio.start_ap(" ", "", channel=channel)
            wifi.radio.stop_ap()
        except Exception as exc:
            print("ESP-NOW MIDI: could not set WiFi channel:", exc)

    # espnow calls

    def _backend_add_peer(self, mac):
        # channel=0: send on whatever channel the radio is on. A fixed channel
        # makes every send raise when the radio is elsewhere (e.g. connected
        # to WiFi); _configure_wifi_channel already moved the radio.
        peer = espnow.Peer(mac=mac, channel=0)
        self.e.peers.append(peer)
        self._peer_objects[mac] = peer

    def _backend_remove_peer(self, mac):
        peer = self._peer_objects.pop(mac, None)
        if peer is not None:
            self.e.peers.remove(peer)

    def _backend_send(self, mac, data):
        """Send to one peer; raises on failure."""
        self.e.send(data, self._peer_objects[mac])

    def _backend_receive(self):
        """Yield (mac, msg) for every packet waiting, without blocking."""
        while True:
            packet = self.e.read()
            if packet is None:
                return
            yield bytes(packet.mac), bytes(packet.msg)

    def _create_packet(self, status, channel, first_byte, second_byte):
        """
        Create MIDI packet matching C++ midi_message_packet::fromMessage

        Returns:
            bytes: Variable length packet (1-3 bytes)
        """
        channel_zero_based = (channel - 1) & 0x0F

        # System messages (0xF0-0xFF) don't use channel bits
        if status >= 0xF0:
            status_byte = status
        else:
            status_byte = status | channel_zero_based

        data1 = first_byte & 0x7F
        data2 = second_byte & 0x7F

        size = self._get_data_size(status_byte)
        if size == 1:
            return bytes([status_byte])
        elif size == 2:
            return bytes([status_byte, data1])
        return bytes([status_byte, data1, data2])

    def _get_data_size(self, status_byte):
        """Size of a short MIDI message (matches C++ getDataSize())."""
        return _midi_data_size(status_byte)

    def add_peer(self, mac_address):
        """
        Add a peer by MAC address

        Args:
            mac_address: bytes of length 6 or list/tuple of 6 integers

        Returns:
            bool: True if peer was added successfully, False otherwise
        """
        if len(self.peers) >= self._max_peers:
            print("Maximum number of peers reached")
            return False

        mac_address = bytes(mac_address)
        if len(mac_address) != 6:
            print("MAC address must be 6 bytes")
            return False

        if mac_address in self.peers:
            print("Peer already registered")
            return False

        mac_str = ':'.join(['%02X' % b for b in mac_address])
        print(f"Adding peer: {mac_str}")

        try:
            self._backend_add_peer(mac_address)
        except Exception as ex:
            print("Failed to add peer:", ex)
            return False
        self.peers.append(mac_address)
        print(f"Peer added successfully. Total peers: {len(self.peers)}")
        return True

    def remove_peer(self, mac_address):
        """
        Remove one peer. Matches C++ removePeer.

        Returns:
            bool: True if the peer was registered and is removed
        """
        mac_address = bytes(mac_address)
        if mac_address not in self.peers:
            return False
        try:
            self._backend_remove_peer(mac_address)
        except Exception:
            print("Failed to remove peer")
        self.peers.remove(mac_address)
        self._last_seen.pop(mac_address, None)
        return True

    def clear_peers(self):
        """
        Remove all registered peers from ESP-NOW
        Matches C++ clearPeers
        """
        print("Clearing all peers from ESP-NOW...")
        for peer in self.peers:
            try:
                self._backend_remove_peer(peer)
                mac_str = ':'.join(['%02X' % b for b in peer])
                print(f"Removed peer: {mac_str}")
            except Exception:
                print("Failed to remove peer")
        self.peers = []
        self._peer_objects = {}
        self._last_seen = {}
        print("All peers cleared")

    def get_peers_count(self):
        """Get the number of registered peers"""
        return len(self.peers)

    def print_peers(self):
        """Print all registered peers (matches C++ format)"""
        print("=== Registered ESP-NOW Peers ===")
        for i, peer in enumerate(self.peers):
            mac_str = ':'.join(['%02X' % b for b in peer])
            print(f"Peer {i}: {mac_str}")
        print("================================")

    def send_to_all_peers(self, data):
        """
        Send data to all registered peers

        Args:
            data: bytes object to send

        Returns:
            bool: True if sent to at least one peer successfully
        """
        if self.e is None or not self.peers:
            return False

        success = False
        for peer in self.peers:
            try:
                self._backend_send(peer, data)
                success = True
            except Exception:
                pass  # Silent failure like C++ version

        if success:
            self._last_send_ms = _now_ms()
        return success

    def get_last_send_time(self):
        """
        When a packet was last sent (ms, same clock as get_peer_last_seen).
        Matches C++ getLastSendTime.

        Returns:
            int or None when nothing was sent yet
        """
        return self._last_send_ms

    def get_peer_last_seen(self, mac):
        """
        When a peer last sent a valid packet (ms). Matches C++ getPeerLastSeen.

        Returns:
            int, or None when the MAC is not a peer or nothing arrived from it
        """
        mac = bytes(mac)
        if mac not in self.peers:
            return None
        return self._last_seen.get(mac)

    def last_sender_mac(self):
        """
        MAC of the peer that sent the last valid packet. Matches C++ lastSenderMac.

        Returns:
            bytes, or None when nothing was received yet
        """
        return self._last_sender_mac

    def send_announce(self, name="", kind=DEVICE_KIND_CLIENT):
        """
        Tell peers who this device is with an enomik ANNOUNCE (0x15), like the
        C++ Client does after begin() and after adding a peer. The dongle keeps
        the name and tracks online status (Dongle::getPeerName/getPeerStatus).

        Args:
            name: up to 16 printable ASCII characters
            kind: DEVICE_KIND_CLIENT or DEVICE_KIND_DONGLE

        Returns:
            bool: True if sent to at least one peer
        """
        name_bytes = bytes(c if 0x20 <= c <= 0x7E else 0x5F
                           for c in name.encode()[:ANNOUNCE_MAX_NAME])
        message = bytes([
            MIDI_SYSEX, ENOMIK_MANUFACTURER_ID, VERSION_MAJOR, VERSION_MINOR,
            ENOMIK_CMD_ANNOUNCE, kind & 0x7F, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
        ]) + name_bytes + bytes([SYSEX_END])
        return self.send_sysex(message)

    def send_note_on(self, note, velocity, channel):
        """
        Send MIDI Note On message
        
        Args:
            note: byte, MIDI note number (0-127)
            velocity: byte, note velocity (0-127)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_NOTE_ON, channel, note, velocity)
        return self.send_to_all_peers(packet)
    
    def send_note_off(self, note, velocity, channel):
        """
        Send MIDI Note Off message
        
        Args:
            note: byte, MIDI note number (0-127)
            velocity: byte, release velocity (0-127)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_NOTE_OFF, channel, note, velocity)
        return self.send_to_all_peers(packet)
    
    def send_control_change(self, control, value, channel):
        """
        Send MIDI Control Change message
        
        Args:
            control: byte, control number (0-127)
            value: byte, control value (0-127)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_CONTROL_CHANGE, channel, control, value)
        return self.send_to_all_peers(packet)
    
    def send_program_change(self, program, channel):
        """
        Send MIDI Program Change message
        
        Args:
            program: byte, program number (0-127)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_PROGRAM_CHANGE, channel, program, 0)
        return self.send_to_all_peers(packet)
    
    def send_after_touch(self, pressure, channel):
        """
        Send MIDI Channel Aftertouch (pressure) message
        Note: Method name uses underscore (send_after_touch) to match Python conventions
        but functionality matches C++ sendAfterTouch(pressure, channel)
        
        Args:
            pressure: byte, pressure value (0-127)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_AFTERTOUCH, channel, pressure, 0)
        return self.send_to_all_peers(packet)
    
    def send_after_touch_poly(self, note, pressure, channel):
        """
        Send MIDI Polyphonic Aftertouch message
        Note: Method name uses underscores to match Python conventions
        but functionality matches C++ sendAfterTouchPoly
        
        Args:
            note: byte, MIDI note number (0-127)
            pressure: byte, pressure value (0-127)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_POLY_AFTERTOUCH, channel, note, pressure)
        return self.send_to_all_peers(packet)
    
    def send_pitch_bend_raw(self, value, channel):
        """
        Send MIDI Pitch Bend message with raw value (0-16383)
        Matches C++ sendPitchBendRaw
        
        Args:
            value: int, 0-16383 (14-bit value, 8192 is center)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        value = value & 0x3FFF  # Clamp to 14 bits
        lsb = value & 0x7F
        msb = (value >> 7) & 0x7F
        packet = self._create_packet(MIDI_PITCH_BEND, channel, lsb, msb)
        return self.send_to_all_peers(packet)
    
    def send_pitch_bend(self, value, channel):
        """
        Send MIDI Pitch Bend message with signed value (-8192 to +8191)
        Matches C++ sendPitchBend
        
        Args:
            value: int16_t, -8192 to +8191 (0 is center)
            channel: byte, MIDI channel (1-16)
        
        Returns:
            bool: True if sent successfully
        """
        # Clamp to signed 14-bit range
        if value < -8192:
            value = -8192
        if value > 8191:
            value = 8191
        
        # Translate signed (-8192..8191) -> unsigned (0..16383)
        raw = value + 8192
        return self.send_pitch_bend_raw(raw, channel)
    
    def send_start(self):
        """
        Send MIDI Start (realtime) message
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_START, 0, 0, 0)
        return self.send_to_all_peers(packet)
    
    def send_stop(self):
        """
        Send MIDI Stop (realtime) message
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_STOP, 0, 0, 0)
        return self.send_to_all_peers(packet)
    
    def send_continue(self):
        """
        Send MIDI Continue (realtime) message
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_CONTINUE, 0, 0, 0)
        return self.send_to_all_peers(packet)
    
    def send_clock(self):
        """
        Send MIDI Clock (timing) message
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_TIME_CLOCK, 0, 0, 0)
        return self.send_to_all_peers(packet)
    
    def send_song_position(self, value):
        """
        Send MIDI Song Position Pointer message
        Matches C++ sendSongPosition
        
        Args:
            value: uint16_t, 0-16383 (14-bit value)
        
        Returns:
            bool: True if sent successfully
        """
        value = value & 0x3FFF  # Clamp to 14 bits
        lsb = value & 0x7F
        msb = (value >> 7) & 0x7F
        packet = self._create_packet(MIDI_SONG_POS_POINTER, 0, lsb, msb)
        return self.send_to_all_peers(packet)
    
    def send_song_select(self, value):
        """
        Send MIDI Song Select message
        Matches C++ sendSongSelect
        
        Args:
            value: uint8_t, 0-127
        
        Returns:
            bool: True if sent successfully
        """
        value = value & 0x7F  # Clamp to 7 bits
        packet = self._create_packet(MIDI_SONG_SELECT, 0, value, 0)
        return self.send_to_all_peers(packet)
    
    def send_tune_request(self):
        """
        Send MIDI Tune Request message
        Matches C++ sendTuneRequest
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_TUNE_REQUEST, 0, 0, 0)
        return self.send_to_all_peers(packet)
    
    def send_time_code(self, value):
        """
        Send MIDI Time Code message
        Matches C++ sendTimeCode
        
        Args:
            value: uint8_t, 0-127
        
        Returns:
            bool: True if sent successfully
        """
        value = value & 0x7F  # Clamp to 7 bits
        packet = self._create_packet(MIDI_TIME_CODE, 0, value, 0)
        return self.send_to_all_peers(packet)
    
    def send_active_sensing(self):
        """
        Send MIDI Active Sensing message
        Matches C++ sendActiveSensing
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_ACTIVE_SENSING, 0, 0, 0)
        return self.send_to_all_peers(packet)
    
    def send_system_reset(self):
        """
        Send MIDI System Reset message
        Matches C++ sendSystemReset
        
        Returns:
            bool: True if sent successfully
        """
        packet = self._create_packet(MIDI_SYSTEM_RESET, 0, 0, 0)
        return self.send_to_all_peers(packet)
    
    def send_sysex(self, data, length=None):
        """
        Send MIDI SysEx message (fragmented if needed).
        Matches C++ sendSysex wire format in esp_now_midi_sysex.h.

        Args:
            data: list/bytes, complete SysEx (typically including F0..F7)
            length: optional length; defaults to len(data)

        Returns:
            bool: True if all fragments sent successfully
        """
        if data is None:
            return False
        if length is None:
            length = len(data)
        if length <= 0 or length > SYSEX_MAX_MESSAGE:
            return False

        total = _sysex_fragment_count(length)
        for seq in range(total):
            offset = seq * SYSEX_MAX_PAYLOAD
            payload_len = _sysex_fragment_payload_length(length, seq)
            payload = bytes(data[offset:offset + payload_len])
            frame = _sysex_encode_frame(seq, total, length, payload)
            if frame is None:
                return False
            if not self.send_to_all_peers(frame):
                return False
        return True

    def _feed_sysex_frame(self, mac, msg):
        """Reassemble fragmented SysEx; return complete bytes or None."""
        frame = _sysex_parse_frame(msg)
        if frame is None:
            return None

        mac_key = bytes(mac) if mac is not None else b""
        now = _now_ms()
        asm = self._sysex_assembly
        if asm is not None and now - asm["last_ms"] > SYSEX_REASSEMBLY_TIMEOUT_MS:
            self._sysex_assembly = None  # fragments stopped arriving
        if frame["seq"] == 0:
            self._sysex_assembly = {
                "mac": mac_key,
                "msg_len": frame["msg_len"],
                "total": frame["total"],
                "expected_seq": 0,
                "buffer": bytearray(frame["msg_len"]),
                "last_ms": now,
            }

        asm = self._sysex_assembly
        if asm is None or asm["mac"] != mac_key:
            return None
        if asm["msg_len"] != frame["msg_len"] or asm["total"] != frame["total"]:
            return None
        if frame["seq"] != asm["expected_seq"]:
            return None

        offset = frame["seq"] * SYSEX_MAX_PAYLOAD
        payload = frame["payload"]
        asm["buffer"][offset:offset + len(payload)] = payload
        asm["expected_seq"] = frame["seq"] + 1
        asm["last_ms"] = now

        if not frame["last"]:
            return None
        if asm["expected_seq"] != asm["total"]:
            return None

        complete = bytes(asm["buffer"])
        self._sysex_assembly = None
        return complete

    def _parse_packet(self, data):
        """
        Parse received packet to internal message format
        Matches C++ midi_message_packet::toMessage()
        
        Args:
            data: bytes, received MIDI packet (1-3 bytes)
        
        Returns:
            tuple: (status, channel, first_byte, second_byte) or None if invalid
        """
        if len(data) > 3 or not is_midi_packet(data):
            return None

        status_byte = data[0]
        data1 = data[1] if len(data) > 1 else 0
        data2 = data[2] if len(data) > 2 else 0
        
        # Extract status and channel
        if status_byte >= 0xF0:
            # System message - no channel
            status = status_byte
            channel = 0
        else:
            # Channel message - extract both
            status = status_byte & 0xF0
            channel = (status_byte & 0x0F) + 1  # Convert 0-based to 1-based
        
        return (status, channel, data1, data2)
    
    def _on_data_recv(self, mac, msg):
        """
        Process one received ESP-NOW packet. Matches C++ OnDataRecv: anything
        that is not a MIDI packet or SysEx frame (other ESP-NOW apps, corrupt
        data) is ignored and does not add its sender as peer.
        """
        if not is_midi_packet(msg):
            return
        if mac is not None:
            mac = bytes(mac)
            self._last_sender_mac = mac
            if (self._auto_peer_discovery and mac not in self.peers
                    and len(self.peers) < self._max_peers):
                self.add_peer(mac)
            if mac in self.peers:
                self._last_seen[mac] = _now_ms()

        # SysEx transport frames are always longer than short MIDI (1-3 bytes).
        if len(msg) > 3:
            complete = self._feed_sysex_frame(mac, msg)
            if complete is not None:
                if self._on_sysex_handler:
                    self._on_sysex_handler(complete, len(complete))
                if self._on_sysex_from_handler:
                    self._on_sysex_from_handler(mac, complete, len(complete))
            return

        # Parse the packet
        result = self._parse_packet(msg)
        if not result:
            return
        
        status, channel, first_byte, second_byte = result

        if self._on_message_handler:
            self._on_message_handler(mac, result)
        
        # Route to appropriate handler (matches C++ switch statement)
        if status == MIDI_NOTE_ON:
            if self._on_note_on_handler:
                self._on_note_on_handler(channel, first_byte, second_byte)
        
        elif status == MIDI_NOTE_OFF:
            if self._on_note_off_handler:
                self._on_note_off_handler(channel, first_byte, second_byte)
        
        elif status == MIDI_CONTROL_CHANGE:
            if self._on_control_change_handler:
                self._on_control_change_handler(channel, first_byte, second_byte)
        
        elif status == MIDI_PROGRAM_CHANGE:
            if self._on_program_change_handler:
                self._on_program_change_handler(channel, first_byte)
        
        elif status == MIDI_AFTERTOUCH:
            if self._on_after_touch_channel_handler:
                self._on_after_touch_channel_handler(channel, first_byte)
        
        elif status == MIDI_POLY_AFTERTOUCH:
            if self._on_after_touch_poly_handler:
                self._on_after_touch_poly_handler(channel, first_byte, second_byte)
        
        elif status == MIDI_PITCH_BEND:
            # Reconstruct 14-bit value and convert to signed (matches C++ logic)
            pitch_bend_value = (second_byte << 7) | first_byte
            signed_value = pitch_bend_value - 8192
            if self._on_pitch_bend_handler:
                self._on_pitch_bend_handler(channel, signed_value)
        
        elif status == MIDI_START:
            if self._on_start_handler:
                self._on_start_handler()
        
        elif status == MIDI_STOP:
            if self._on_stop_handler:
                self._on_stop_handler()
        
        elif status == MIDI_CONTINUE:
            if self._on_continue_handler:
                self._on_continue_handler()
        
        elif status == MIDI_TIME_CLOCK:
            if self._on_clock_handler:
                self._on_clock_handler()
        
        elif status == MIDI_SONG_POS_POINTER:
            song_pos_value = (second_byte << 7) | first_byte
            if self._on_song_position_handler:
                self._on_song_position_handler(song_pos_value)
        
        elif status == MIDI_SONG_SELECT:
            if self._on_song_select_handler:
                self._on_song_select_handler(first_byte)
        
        elif status == MIDI_TIME_CODE:
            if self._on_time_code_handler:
                self._on_time_code_handler(first_byte)
        
        elif status == MIDI_ACTIVE_SENSING:
            if self._on_active_sensing_handler:
                self._on_active_sensing_handler()
        
        elif status == MIDI_SYSTEM_RESET:
            if self._on_system_reset_handler:
                self._on_system_reset_handler()
    
    def read(self):
        """
        Poll for incoming MIDI messages and dispatch to handlers
        
        Note: Unlike C++, CircuitPython has no receive callback; poll in the main loop.
        Call this method regularly (e.g., in while True loop)
        
        Usage:
            while True:
                midi.read()
                # your code here
        """
        if self.e is None:
            return
        for mac, msg in self._backend_receive():
            self._on_data_recv(mac, msg)

    # Handler setters (matching C++ setHandleXXX methods)
    
    def set_handle_note_on(self, callback):
        """
        Set handler for Note On messages
        Matches C++ setHandleNoteOn
        
        Args:
            callback: function(channel, note, velocity)
        """
        self._on_note_on_handler = callback
    
    def set_handle_note_off(self, callback):
        """
        Set handler for Note Off messages
        Matches C++ setHandleNoteOff
        
        Args:
            callback: function(channel, note, velocity)
        """
        self._on_note_off_handler = callback
    
    def set_handle_control_change(self, callback):
        """
        Set handler for Control Change messages
        Matches C++ setHandleControlChange
        
        Args:
            callback: function(channel, control, value)
        """
        self._on_control_change_handler = callback
    
    def set_handle_program_change(self, callback):
        """
        Set handler for Program Change messages
        Matches C++ setHandleProgramChange
        
        Args:
            callback: function(channel, program)
        """
        self._on_program_change_handler = callback
    
    def set_handle_pitch_bend(self, callback):
        """
        Set handler for Pitch Bend messages
        Matches C++ setHandlePitchBend
        
        Args:
            callback: function(channel, value)
                     value is signed: -8192 to +8191 (0 is center)
        """
        self._on_pitch_bend_handler = callback
    
    def set_handle_after_touch_channel(self, callback):
        """
        Set handler for Channel Aftertouch messages
        Matches C++ setHandleAfterTouchChannel
        
        Args:
            callback: function(channel, pressure)
        """
        self._on_after_touch_channel_handler = callback
    
    def set_handle_after_touch_poly(self, callback):
        """
        Set handler for Polyphonic Aftertouch messages
        Matches C++ setHandleAfterTouchPoly
        
        Args:
            callback: function(channel, note, pressure)
        """
        self._on_after_touch_poly_handler = callback
    
    def set_handle_start(self, callback):
        """
        Set handler for Start messages
        Matches C++ setHandleStart
        
        Args:
            callback: function()
        """
        self._on_start_handler = callback
    
    def set_handle_stop(self, callback):
        """
        Set handler for Stop messages
        Matches C++ setHandleStop
        
        Args:
            callback: function()
        """
        self._on_stop_handler = callback
    
    def set_handle_continue(self, callback):
        """
        Set handler for Continue messages
        Matches C++ setHandleContinue
        
        Args:
            callback: function()
        """
        self._on_continue_handler = callback
    
    def set_handle_clock(self, callback):
        """
        Set handler for Clock messages
        Matches C++ setHandleClock
        
        Args:
            callback: function()
        """
        self._on_clock_handler = callback
    
    def set_handle_song_position(self, callback):
        """
        Set handler for Song Position messages
        Matches C++ setHandleSongPosition
        
        Args:
            callback: function(value)
                     value is uint16: 0-16383
        """
        self._on_song_position_handler = callback
    
    def set_handle_song_select(self, callback):
        """
        Set handler for Song Select messages
        Matches C++ setHandleSongSelect
        
        Args:
            callback: function(value)
                     value is byte: 0-127
        """
        self._on_song_select_handler = callback
    
    def set_handle_time_code(self, callback):
        """
        Set handler for Time Code messages
        Matches C++ setHandleTimeCode
        
        Args:
            callback: function(value)
                     value is byte: 0-127
        """
        self._on_time_code_handler = callback
    
    def set_handle_active_sensing(self, callback):
        """
        Set handler for Active Sensing messages
        Matches C++ setHandleActiveSensing
        
        Args:
            callback: function()
        """
        self._on_active_sensing_handler = callback
    
    def set_handle_system_reset(self, callback):
        """
        Set handler for System Reset messages
        Matches C++ setHandleSystemReset
        
        Args:
            callback: function()
        """
        self._on_system_reset_handler = callback

    def set_handle_sysex(self, callback):
        """
        Set handler for complete SysEx messages after reassembly.
        Matches C++ setHandleSysEx

        Args:
            callback: function(data, length) where data is bytes
        """
        self._on_sysex_handler = callback

    def set_handle_sysex_from(self, callback):
        """
        Like set_handle_sysex, with the sender MAC. Called after the
        set_handle_sysex handler when both are set. Matches C++ setHandleSysExFrom.

        Args:
            callback: function(mac, data, length)
        """
        self._on_sysex_from_handler = callback

    def set_handle_message(self, callback):
        """
        One handler for every received short MIDI message, before the per-type
        handlers. Matches C++ setHandleMessage.

        Args:
            callback: function(mac, message) where message is the tuple
                      (status, channel, first_byte, second_byte); channel is
                      1-16, or 0 for system messages
        """
        self._on_message_handler = callback
    
    def has_peer(self, mac):
        """
        Check if a peer is registered
        Matches C++ hasPeer
        
        Args:
            mac: bytes, MAC address to check
        
        Returns:
            bool: True if peer exists
        """
        if isinstance(mac, (list, tuple)):
            mac = bytes(mac)
        return mac in self.peers
    
    def deinit(self):
        """Clean up ESP-NOW resources and forget peers and partial SysEx."""
        if self.e is not None:
            try:
                self.clear_peers()
                self.e.deinit()
            except Exception:
                pass
        self.e = None
        self.peers = []
        self._peer_objects = {}
        self._last_seen = {}
        self._sysex_assembly = None
