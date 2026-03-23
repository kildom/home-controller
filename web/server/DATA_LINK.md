# Data Link Layer Implementation

This module implements Layer 2 (Data Link Layer) of the home-controller protocol, providing reliable packet transmission with CRC32 validation and collision avoidance through byte masking.

## Overview

The Data Link Layer handles:
- **Packet encoding**: Adds framing, masking, and CRC32 checksum
- **Packet decoding**: Parses raw serial data, validates CRC32, handles fragmentation
- **Byte masking**: Avoids ESC (0xFF) and STOP (0xFE) markers in transmitted data
- **Fragmented packet handling**: Reconstructs packets from misaligned/fragmented serial input

## Features

- ✅ Full protocol compliance with specification
- ✅ Robust CRC32 validation
- ✅ Automatic MASK calculation to avoid marker bytes
- ✅ Handles fragmented and misaligned packets
- ✅ Maximum data payload: 249 bytes per packet
- ✅ Async/await support
- ✅ Comprehensive test coverage

## Installation

Place `data_link.py` in your server directory alongside `serial_daemon.py`.

## Usage

### Sending Data

```python
import asyncio
import data_link
import serial_daemon

async def main():
    # Start serial daemon first
    await serial_daemon.start()
    await data_link.init()
    
    # Send data
    await data_link.data_link_send(b"Hello, Device!", end_with_stop=True)
    
    # Clean up
    await data_link.deinit()
    await serial_daemon.stop()

asyncio.run(main())
```

### Receiving Data

```python
import asyncio
import data_link
import serial_daemon

async def on_data_received(data: bytes):
    """Called when a valid packet is received"""
    print(f"Received: {data}")

async def main():
    # Start serial daemon first
    await serial_daemon.start()
    await data_link.init()
    
    # Set up callback for received packets
    await data_link.set_data_received_callback(on_data_received)
    
    # Keep running to receive data
    try:
        await asyncio.sleep(3600)  # Run for 1 hour
    except KeyboardInterrupt:
        pass
    
    # Clean up
    await data_link.deinit()
    await serial_daemon.stop()

asyncio.run(main())
```

## API Reference

### `async def data_link_send(data: bytes, end_with_stop: bool = True) -> None`

Send data via the Data Link Layer.

**Parameters:**
- `data`: Bytes or bytearray to transmit (0-249 bytes)
- `end_with_stop`: If True, append STOP byte to packet (default: True)

**Raises:**
- `ValueError`: If data exceeds 249 bytes
- `Exception`: If serial daemon not running

**Example:**
```python
await data_link.data_link_send(b"sensor_data", end_with_stop=True)
```

### `async def set_data_received_callback(callback: Callable[[bytes], None]) -> None`

Register callback function for received packets.

**Parameters:**
- `callback`: Async function that receives decoded packet data

**Example:**
```python
async def handle_packet(data: bytes):
    print(f"Got packet: {data.hex()}")

await data_link.set_data_received_callback(handle_packet)
```

### `async def init() -> None`

Initialize the Data Link Layer (called after serial daemon is running).

### `async def deinit() -> None`

Clean up Data Link Layer resources.

## Packet Format

Packets follow this structure as per protocol specification:

```
┌─────┬──────┬───────────┬────────────┬─────┬──────┐
│ ESC │ MASK │ DATA^MASK │ CRC32^MASK │ ESC │ STOP │
└─────┴──────┴───────────┴────────────┴─────┴──────┘
  1B    1B    0-249B        4B         1B    1B
```

Where:
- **ESC**: Frame marker (0xFF)
- **MASK**: Byte to XOR data/CRC32 (chosen to avoid ESC/STOP)
- **DATA^MASK**: User data XORed with MASK
- **CRC32^MASK**: CRC32 of original data, XORed with MASK  
- **STOP**: End marker (0xFE), optional based on `end_with_stop`

## MASK Calculation

The MASK byte is calculated to ensure that no byte in the encoded data matches ESC (0xFF) or STOP (0xFE):

1. If 0xFF doesn't appear in data+CRC32 → MASK = 0
2. Otherwise:
   - Mark 0x00 and 0x01 as reserved
   - Find unused byte `x`
   - MASK = x ⊕ 0xFF

This ensures XORing any data byte with MASK cannot produce 0xFF or 0xFE.

## Error Handling

The parser is robust against:
- **Fragmented packets**: Buffers incomplete data until complete
- **Misaligned packets**: Finds ESC markers and re-synchronizes
- **Invalid CRC**: Discards packets with CRC mismatch
- **Too-short packets**: Rejects packets < 5 bytes (MASK + 4-byte CRC32)
- **Double ESC bytes**: Treats as packet boundary or new packet start

Invalid packets are logged but don't crash the parser—it continues searching for the next valid packet.

## Limitations

- Maximum 249 bytes per packet (per protocol specification)
- Requires Python 3.7+ with `asyncio` support
- Depends on `serial_daemon.py` for serial port I/O

## Testing

Run the included test suite:

```bash
python3 test_data_link.py
```

Tests cover:
- CRC32 calculation consistency
- MASK calculation correctness
- Full encode/decode round-trip
- Error cases (oversized data, bad CRC, short packets)
- Packet format validation

## Protocol Notes

See `protocol.md` for the complete protocol specification, including:
- Physical layer (UART/CAN)
- Collision handling
- Multiple packets in sequence

## License

Part of home-controller project
