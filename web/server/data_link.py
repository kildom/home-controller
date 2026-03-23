"""
Data Link Layer implementation for home-controller protocol.

Handles packet encoding/decoding according to the protocol specification.
Provides functions for sending and receiving data with proper framing, 
masking, and CRC32 validation.
"""

import asyncio
import struct
import zlib
from typing import Callable, Optional

import serial_daemon


# Constants
ESC = 0xFF
STOP = 0xFE
MAX_DATA_LEN = 249


# Global state
_parser_buffer = bytearray()
_data_received_callback: Optional[Callable] = None


def _calculate_crc32(data: bytes) -> bytes:
    """Calculate CRC32 of data and return as 4 bytes (little-endian)"""
    crc = zlib.crc32(data) & 0xFFFFFFFF
    return struct.pack('<I', crc)


def _calculate_mask(data: bytes, crc32: bytes) -> int:
    """
    Calculate MASK byte to avoid ESC (0xFF) and STOP (0xFE) in output.
    
    Algorithm:
    1. If 0xFF doesn't appear in data+CRC32, use MASK=0
    2. Mark 0x00 and 0x01 as reserved
    3. Find any byte that doesn't appear in (data+CRC32+reserved)
    4. Use MASK = that_byte XOR 0xFF
    """
    combined = bytearray(data) + bytearray(crc32)
    
    # If 0xFF (ESC) doesn't occur, MASK = 0
    if ESC not in combined:
        return 0
    
    # Build set of used bytes and mark 0x00, 0x01 as reserved
    used = set(combined) | {0x00, 0x01}
    
    # Find an unused byte
    for x in range(256):
        if x not in used:
            # Use MASK = x XOR ESC so that ESC becomes x when masked
            mask = x ^ ESC
            return mask
    
    # Should never reach here - there are always unused bytes
    # (max 253 bytes of data + reserved 2 = 255 < 256)
    return 0


def _encode_packet(data: bytes, end_with_stop: bool) -> bytes:
    """
    Encode data according to Data Link Layer specification.
    
    Format:
        | ESC | MASK | DATA^MASK | CRC32^MASK | ESC | [STOP] |
    
    Args:
        data: Bytes to send
        end_with_stop: If True, append STOP byte after final ESC
    
    Returns:
        Encoded packet ready to send
    """
    if len(data) > MAX_DATA_LEN:
        raise ValueError(f"Data too long: {len(data)} > {MAX_DATA_LEN}")
    
    crc32 = _calculate_crc32(data)
    mask = _calculate_mask(data, crc32)
    
    packet = bytearray()
    
    # ESC + MASK at start
    packet.append(ESC)
    packet.append(mask)
    
    # Masked data
    for byte in data:
        packet.append(byte ^ mask)
    
    # Masked CRC32
    for byte in crc32:
        packet.append(byte ^ mask)
    
    # ESC + [STOP] at end
    packet.append(ESC)
    if end_with_stop:
        packet.append(STOP)
    
    return bytes(packet)


def _decode_packet(packet_data: bytes) -> Optional[bytes]:
    """
    Decode a Data Link Layer packet.
    
    Args:
        packet_data: Bytes between ESC markers (including MASK, data, CRC32)
    
    Returns:
        Decoded data bytes if CRC valid, None if CRC invalid
    
    Raises:
        ValueError: If packet too short or invalid format
    """
    if len(packet_data) < 5:  # At least: MASK + 0 bytes data + 4 bytes CRC32
        raise ValueError(f"Packet too short: {len(packet_data)} < 5")
    
    mask = packet_data[0]
    content = packet_data[1:]  # Everything after MASK
    
    # Last 4 bytes are CRC32 (masked)
    if len(content) < 4:
        raise ValueError("Packet too short for CRC32")
    
    crc32_masked = bytes(content[-4:])
    data_masked = bytes(content[:-4])
    
    # Unmask everything
    data = bytes(b ^ mask for b in data_masked)
    crc32_bytes = bytes(b ^ mask for b in crc32_masked)
    
    # Validate CRC32
    expected_crc32 = _calculate_crc32(data)
    if crc32_bytes != expected_crc32:
        raise ValueError(
            f"CRC32 mismatch: got {crc32_bytes.hex()}, "
            f"expected {expected_crc32.hex()}"
        )
    
    return data


async def _parse_incoming_data(raw_data: bytes) -> None:
    """
    Parse incoming raw data from serial port.
    
    Handles fragmented/misaligned packets:
    - Looks for ESC markers to identify packet boundaries
    - Accumulates data between ESC markers
    - When STOP byte received, packet is complete
    - Invalid packets are discarded and parsing continues
    
    Args:
        raw_data: Raw bytes received from serial port
    """
    global _parser_buffer
    
    # Add incoming data to buffer
    _parser_buffer.extend(raw_data)
    
    # Process complete packets from buffer
    while True:
        # Find first ESC marker (start of packet)
        start_esc = _parser_buffer.find(ESC)
        if start_esc == -1:
            # No complete packet marker found, clear buffer and wait
            _parser_buffer.clear()
            break
        
        # Remove everything before the first ESC
        if start_esc > 0:
            _parser_buffer = _parser_buffer[start_esc:]
        
        # Look for second ESC marker (end of content)
        end_esc = _parser_buffer.find(ESC, 1)  # Start search after first ESC
        if end_esc == -1:
            # Incomplete packet, wait for more data
            break
        
        # Extract packet content (between the two ESC markers)
        packet_content = _parser_buffer[1:end_esc]
        
        # Check what comes after the ending ESC
        # Valid endings: ESC alone, ESC+STOP, or nothing yet (incomplete)
        if end_esc + 1 < len(_parser_buffer):
            next_byte = _parser_buffer[end_esc + 1]
            
            if next_byte == STOP:
                # Complete packet with STOP marker
                try:
                    decoded = _decode_packet(packet_content)
                    if _data_received_callback:
                        await _data_received_callback(decoded)
                except Exception as e:
                    # Invalid packet - log and continue
                    print(f"Data Link: Invalid packet: {e}")
                
                # Remove processed packet from buffer (including STOP)
                _parser_buffer = _parser_buffer[end_esc + 2:]
            
            elif next_byte == ESC:
                # ESC without STOP - treat as incomplete and start new packet from this ESC
                # According to spec: "If BEGIN appears again, terminate the current packet"
                # Just skip the current packet and let the next ESC be processed
                _parser_buffer = _parser_buffer[end_esc:]
                continue
            
            else:
                # Unknown byte after ESC - likely garbage/misalignment
                # Skip this packet and continue
                _parser_buffer = _parser_buffer[end_esc + 1:]
        else:
            # Incomplete - we have ESC but don't know what follows
            # Wait for more data
            break


async def data_link_send(data: bytes, end_with_stop: bool = True) -> None:
    """
    Send data via Data Link Layer.
    
    Encodes data according to protocol specification and sends via serial daemon.
    
    Args:
        data: Bytes or bytearray to send
        end_with_stop: If True, append STOP byte to packet
    
    Raises:
        ValueError: If data too long (> MAX_DATA_LEN)
        Exception: If serial daemon not running
    """
    data_bytes = bytes(data)
    
    # Encode packet
    packet = _encode_packet(data_bytes, end_with_stop)

    # Send raw packet bytes through serial daemon.
    await serial_daemon.send_raw(packet)


async def set_data_received_callback(callback: Callable[[bytes], None]) -> None:
    """
    Set callback to be called when valid packet is received.
    
    Args:
        callback: Async function(data: bytes) called with decoded packet data
    """
    global _data_received_callback
    _data_received_callback = callback


async def init() -> None:
    """Initialize Data Link Layer (called after serial daemon is running)"""
    global _parser_buffer
    _parser_buffer = bytearray()
    # Always attach parser internally; consumers can optionally set a callback.
    serial_daemon.set_data_received_callback(_parse_incoming_data)


async def deinit() -> None:
    """Deinitialize Data Link Layer"""
    global _parser_buffer, _data_received_callback
    serial_daemon.remove_data_received_callback(_parse_incoming_data)
    _parser_buffer = bytearray()
    _data_received_callback = None
