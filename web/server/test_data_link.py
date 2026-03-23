"""
Test suite for Data Link Layer implementation.
"""

import sys
import struct
from pathlib import Path

# Add server directory to path
sys.path.insert(0, str(Path(__file__).parent))

# Mock serial_daemon for testing
class MockSerialDaemon:
    """Mock serial daemon for testing"""
    @staticmethod
    async def send_raw(data):
        return None
    
    @staticmethod
    def set_data_received_callback(callback):
        pass

    @staticmethod
    def remove_data_received_callback(callback):
        pass

sys.modules['serial_daemon'] = MockSerialDaemon()

import data_link


def test_crc32():
    """Test CRC32 calculation"""
    print("Testing CRC32 calculation...")
    
    test_data = b"Hello, World!"
    crc = data_link._calculate_crc32(test_data)
    
    # Should always return 4 bytes
    assert len(crc) == 4, f"CRC32 should be 4 bytes, got {len(crc)}"
    
    # Same input should give same CRC
    crc2 = data_link._calculate_crc32(test_data)
    assert crc == crc2, "CRC32 not consistent"
    
    # Different input should give different CRC
    crc3 = data_link._calculate_crc32(b"Different")
    assert crc != crc3, "Different inputs should have different CRC"
    
    print("  ✓ CRC32 calculation works")


def test_mask_calculation():
    """Test MASK calculation"""
    print("Testing MASK calculation...")
    
    # Case 1: No 0xFF in data - should return 0
    mask = data_link._calculate_mask(b"Hello", b"\x00\x01\x02\x03")
    assert mask == 0, f"Expected mask=0 when no ESC in data, got {mask:02x}"
    
    # Case 2: 0xFF in data - should return non-zero
    mask = data_link._calculate_mask(b"\xFF\x01\x02", b"\x03\x04\x05\x06")
    assert mask != 0, "Expected non-zero mask when ESC in data"
    
    # Case 3: Mask should not map 0xFF to 0xFF or 0xFE
    data = b"\xFF\x01\x02\x03"
    crc = b"\x04\x05\x06\x07"
    mask = data_link._calculate_mask(data, crc)
    
    # Check that masked values don't have ESC or STOP
    for byte in data + crc:
        masked = byte ^ mask
        assert masked != 0xFF, f"Mask {mask:02x} maps {byte:02x} to ESC (0xFF)"
        assert masked != 0xFE, f"Mask {mask:02x} maps {byte:02x} to STOP (0xFE)"
    
    print("  ✓ MASK calculation works")


def test_encode_decode():
    """Test encoding and decoding"""
    print("Testing encode/decode...")
    
    test_cases = [
        b"",  # Empty data
        b"Hello",  # Simple data
        b"X" * 249,  # Maximum data size
        bytes(range(200)),  # Various byte values
        b"\xFF\xFE\x00\x01",  # Special marker bytes
    ]
    
    for data in test_cases:
        # Encode with STOP
        packet = data_link._encode_packet(data, end_with_stop=True)
        
        # Packet should start and end with ESC
        assert packet[0] == 0xFF, "Packet should start with ESC"
        
        # Find the end ESC and STOP
        last_esc_idx = packet.rfind(0xFF, 1)  # Find ESC after first one
        assert last_esc_idx != -1, "Packet should have ending ESC"
        assert packet[last_esc_idx + 1] == 0xFE, "STOP should follow ending ESC"
        
        # Extract content (between ESCs)
        content = packet[1:last_esc_idx]
        
        # Decode
        decoded = data_link._decode_packet(content)
        assert decoded == data, f"Decoded data doesn't match: {decoded!r} != {data!r}"
        
        print(f"  ✓ Encode/decode works for {len(data)} byte(s)")


def test_encode_errors():
    """Test encoding error cases"""
    print("Testing encoding error cases...")
    
    # Data too long
    try:
        data_link._encode_packet(b"X" * 250, end_with_stop=True)
        assert False, "Should reject data > 249 bytes"
    except ValueError as e:
        assert "too long" in str(e).lower()
        print("  ✓ Rejects oversized data")


def test_decode_errors():
    """Test decoding error cases"""
    print("Testing decoding error cases...")
    
    # Too short
    try:
        data_link._decode_packet(b"\x00\x01\x02\x03")
        assert False, "Should reject packet < 5 bytes"
    except ValueError:
        print("  ✓ Rejects too-short packets")
    
    # Invalid CRC
    try:
        bad_packet = bytearray(b"\x00")  # MASK=0
        bad_packet.extend(b"test")  # Data
        bad_packet.extend(b"\x00\x00\x00\x00")  # Bad CRC
        data_link._decode_packet(bytes(bad_packet))
        assert False, "Should reject packet with invalid CRC"
    except ValueError as e:
        assert "CRC" in str(e)
        print("  ✓ Rejects packets with invalid CRC")


def test_packet_format():
    """Test that packet format matches specification"""
    print("Testing packet format...")
    
    data = b"TEST"
    packet = data_link._encode_packet(data, end_with_stop=True)
    
    # Structure: ESC | MASK | DATA^MASK | CRC32^MASK | ESC | STOP
    # Minimum: 1 + 1 + 0 + 4 + 1 + 1 = 8 bytes
    assert len(packet) >= 8, f"Packet too short: {len(packet)} < 8"
    
    # First byte is ESC
    assert packet[0] == 0xFF
    
    # Second byte is MASK
    mask = packet[1]
    
    # Verify no unmasked ESC or STOP in the middle
    assert 0xFF not in packet[1:-2], "ESC should not appear in packet content"
    assert 0xFE not in packet[1:-2], "STOP should not appear in packet content"
    
    # Last two bytes should be ESC + STOP
    assert packet[-2] == 0xFF
    assert packet[-1] == 0xFE
    
    print("  ✓ Packet format is correct")


def main():
    """Run all tests"""
    print("Running Data Link Layer tests...\n")
    
    try:
        test_crc32()
        test_mask_calculation()
        test_encode_decode()
        test_encode_errors()
        test_decode_errors()
        test_packet_format()
        
        print("\n✅ All tests passed!")
        return 0
    except AssertionError as e:
        print(f"\n❌ Test failed: {e}")
        return 1
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return 1


if __name__ == '__main__':
    sys.exit(main())
