"""
ESP-NOW MIDI Library for CircuitPython
Handles MIDI message transmission over ESP-NOW protocol
Based on the C++ esp_now_midi library - packet format compatible
"""

import espnow

# Keep in sync with version.h
VERSION_MAJOR = 0
VERSION_MINOR = 14
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


class ESPNowMidi:
    """
    ESP-NOW MIDI communication class
    
    Packet format matches C++ midi_message_packet exactly:
    - statusByte: status (upper 4 bits) | channel 0-15 (lower 4 bits)
    - data1: first data byte (0-127)
    - data2: second data byte (0-127)
    
    Internal format (not transmitted):
    - channel: 1-16 (user-facing, converted to 0-15 for transmission)
    - status: MIDI status byte
    - firstByte: first data byte
    - secondByte: second data byte
    """
    
    def __init__(self):
        self.e = None
        self.peers = []
        self._max_peers = MAX_PEERS
        self._auto_peer_discovery = True
        
        # MIDI Handlers (matching C++ private members)
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
    
    def begin(self, reduce_power_at_cost_of_latency=False, auto_peer_discovery=True, channel=ESP_NOW_MIDI_CHANNEL):
        """
        Initialize ESP-NOW
        
        Args:
            reduce_power_at_cost_of_latency: Not implemented in CircuitPython
            auto_peer_discovery: Enable automatic peer discovery on receive
            channel: WiFi channel for ESP-NOW (default matches C++ ESP_NOW_MIDI_CHANNEL)

        Returns:
            True when ESP-NOW is ready.
        """
        self._auto_peer_discovery = auto_peer_discovery
        self._configure_wifi_channel(channel)
        try:
            self.e = espnow.ESPNow()
            self.e.active(True)
        except Exception as exc:
            print("ESP-NOW MIDI init failed:", exc)
            return False
        print("ESP-NOW MIDI initialized")
        return True
    
    def _configure_wifi_channel(self, channel):
        """Best-effort WiFi channel setup before ESP-NOW init."""
        try:
            import wifi
            wifi.radio.configure(channel=channel)
        except (ImportError, AttributeError, TypeError):
            try:
                import network
                wlan = network.WLAN(network.STA_IF)
                if not wlan.active():
                    wlan.active(True)
            except ImportError:
                pass
    
    def _create_packet(self, status, channel, first_byte, second_byte):
        """
        Create MIDI packet matching C++ midi_message_packet::fromMessage
        
        Converts internal message format to 3-byte transmission packet:
        - Combines status and 0-based channel into statusByte
        - Ensures data bytes are 7-bit
        
        Returns:
            bytes: Variable length packet (1-3 bytes)
        """
        # Convert 1-based channel to 0-based
        channel_zero_based = (channel - 1) & 0x0F
        
        # System messages (0xF0-0xFF) don't use channel bits
        if status >= 0xF0:
            status_byte = status
        else:
            status_byte = status | channel_zero_based
        
        data1 = first_byte & 0x7F
        data2 = second_byte & 0x7F
        
        # Get actual data size (matches C++ getDataSize())
        size = self._get_data_size(status_byte)
        
        if size == 1:
            return bytes([status_byte])
        elif size == 2:
            return bytes([status_byte, data1])
        else:  # size == 3
            return bytes([status_byte, data1, data2])
    
    def _get_data_size(self, status_byte):
        """
        Get the size of actual MIDI data (matches C++ getDataSize())
        
        Returns:
            int: Number of bytes (1, 2, or 3)
        """
        status = status_byte & 0xF0
        
        # System messages
        if status_byte >= 0xF0:
            if status_byte in (MIDI_TIME_CODE, MIDI_SONG_SELECT):
                return 2  # status + 1 data byte
            elif status_byte == MIDI_SONG_POS_POINTER:
                return 3  # status + 2 data bytes
            elif status_byte in (MIDI_TUNE_REQUEST, MIDI_TIME_CLOCK, 
                                MIDI_START, MIDI_CONTINUE, MIDI_STOP,
                                MIDI_ACTIVE_SENSING, MIDI_SYSTEM_RESET):
                return 1  # status only
            else:
                return 3  # default to full size
        
        # Channel messages
        if status in (MIDI_PROGRAM_CHANGE, MIDI_AFTERTOUCH):
            return 2  # status + 1 data byte
        else:
            return 3  # status + 2 data bytes
    
    def add_peer(self, mac_address):
        """
        Add a peer by MAC address
        
        Args:
            mac_address: bytes object of length 6 (e.g., b'\\xff\\xff\\xff\\xff\\xff\\xff')
                        or list/tuple of 6 integers
        
        Returns:
            bool: True if peer was added successfully, False otherwise
        """
        if len(self.peers) >= self._max_peers:
            print(f"Maximum number of peers reached")
            return False
        
        # Convert to bytes if needed
        if isinstance(mac_address, (list, tuple)):
            mac_address = bytes(mac_address)
        
        if len(mac_address) != 6:
            print("MAC address must be 6 bytes")
            return False
        
        # Check if peer already exists
        if mac_address in self.peers:
            print("Peer already registered")
            return False
        
        # Debug print (matches C++ output)
        mac_str = ':'.join(['%02X' % b for b in mac_address])
        print(f"Adding peer: {mac_str}")
        
        try:
            self.e.add_peer(mac_address)
            self.peers.append(mac_address)
            print(f"Peer added successfully. Total peers: {len(self.peers)}")
            return True
        except Exception as ex:
            print(f"Failed to add peer")
            return False
    
    def clear_peers(self):
        """
        Remove all registered peers from ESP-NOW
        Matches C++ clearPeers
        """
        print("Clearing all peers from ESP-NOW...")
        for peer in self.peers:
            try:
                self.e.remove_peer(peer)
                mac_str = ':'.join(['%02X' % b for b in peer])
                print(f"Removed peer: {mac_str}")
            except Exception:
                print("Failed to remove peer")
        self.peers = []
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
        if not self.peers:
            return False
        
        success = False
        for peer in self.peers:
            try:
                self.e.send(peer, data)
                success = True
            except Exception as ex:
                pass  # Silent failure like C++ version
        
        return success
    
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
    
    def send_sysex(self, data, length):
        """
        Send MIDI SysEx message
        Matches C++ sendSysex
        
        Args:
            data: list/bytes, SysEx data (max 128 bytes)
            length: uint8_t, length of data
        
        Returns:
            bool: True if sent successfully
        """
        # Create sysex message structure matching C++ midi_sysex_message
        # struct: data[128] + length byte at offset 128
        length = min(length, 128)
        sysex_data = bytearray(129)
        for i in range(length):
            sysex_data[i] = data[i]
        sysex_data[128] = length
        
        return self.send_to_all_peers(bytes(sysex_data))
    
    def _parse_packet(self, data):
        """
        Parse received packet to internal message format
        Matches C++ midi_message_packet::toMessage()
        
        Args:
            data: bytes, received MIDI packet (1-3 bytes)
        
        Returns:
            tuple: (status, channel, first_byte, second_byte) or None if invalid
        """
        if len(data) < 1:
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
        Internal callback for processing received MIDI messages
        Matches C++ OnDataRecv logic
        
        Args:
            mac: bytes, MAC address of sender
            msg: bytes, received message data
        """
        # Auto peer discovery (matches C++ behavior)
        if self._auto_peer_discovery and mac not in self.peers:
            self.add_peer(mac)
        
        # Handle SysEx separately (larger than 3 bytes)
        if len(msg) > 3:
            # TODO: Handle SysEx message if needed
            return
        
        # Parse the packet
        result = self._parse_packet(msg)
        if not result:
            return
        
        status, channel, first_byte, second_byte = result
        
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
        
        Note: Unlike C++, CircuitPython requires polling in main loop.
        Call this method regularly (e.g., in while True loop)
        
        Usage:
            while True:
                midi.read()
                # your code here
        """
        if not self.e:
            return
        
        # Process all pending messages
        for mac, msg in self.e:
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
        """Clean up ESP-NOW resources"""
        if self.e:
            self.e.active(False)