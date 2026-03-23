
# Network Layers

## 1. Physical Layer

* Medium:
  * CAN (only the electrical part – the MCP2562 driver itself), multiple devices on the same bus
  * UART (open collector on TX + pull-up on RX), multiple devices on the same line
  * UART full duplex one-to-one connection, two devices as usual UARTs, no collision handling needed, both devices can transmit at the same time
* Collision Handling
  * All devices are equal (peer-to-peer)
  * The link is considered available if the last packet has finished or nothing has been transmitted for an appropriate time (if the last packet is unknown)
  * If a device wants to transmit, it waits for a random time (distribution depends on queue length, waiting time of the first packet in the queue, and the highest packet priority in the queue)
    * OPTION 1 (probably better) – Sends bytes: ESC, 2–3 random bytes, device address, ESC (or bytes like in option 2)
      * After each sent byte, it waits briefly to receive and confirm correct transmission and detect collisions
      * If a collision occurs, it returns to waiting for a free line. For collisions outside ESC, waiting time is proportional to `clz(expected_byte XOR transmitted_byte)`.  
        For ESC, it is a random time.
      * There are 3–4 bytes between ESC characters, so it won’t be interpreted as a data packet.
    * OPTION 2 – faster and simpler, but higher probability of multiple consecutive collisions
      * Sends a frame (with 2 stop bits): ESC, 4 bytes (where the lower 2 bits are address, 5 random bits, MSB = 1), ESC (UART is LSB first)
        ```
          IDLE S DDDDDDDD ss S DDDDDDDD ss S DDDDDDDD ss S DDDDDDDD ss S DDDDDDDD ss S DDDDDDDD ss IDLE    | S - start bit, s - stop bit, D - data bit
        ------ 0 01010101 11 0 AARRRRR1 11 0 AARRRRR1 11 0 AARRRRR1 11 0 AARRRRR1 11 0 01010101 11 ------  | A - address bit, R - random bit
        ```
      * This frame structure ensures that even if transmitters are shifted by 1 bit, transmission remains valid
      * If more than one transmitter is active, transmission errors will occur, so this must be handled
      * After sending, it checks whether the frame is correct; if yes, it takes control of the line; otherwise, it waits a calculated time
      * Time calculation:
        * Ignore `ESC` bytes
        * `x = sent_byte XOR received_byte` corresponds to collision on expected HIGH state
        * If bits are not too misaligned, each transmitter will have 1 bits at different positions
        * If misaligned, one transmitter will have 1 bits where it sent 0; `d = 0 or 1` depending on whether this occurs
        * Waiting time is proportional to: `T + clz(x) + 32 * d`
        * If only `ESC` bytes were modified, wait `T + rand(0, 63)`
    * Sends the rest of the packet
  * If the packet stops flowing for a defined time, the state is reset

---

## 2. Data Link Layer

* Packet format:
  ```
  |  1  |  1   |   len     |     4      |  1  |  1   |      ESC = 0xFF
  | ESC | MASK | DATA^MASK | CRC32^MASK | ESC | STOP |      STOP = 0xFE
  |   BEGIN    |        CONTENT         |    END     |      len = 0..249
  ```

* How to determine `MASK`:
  * Build a map of symbol occurrences in `DATA` and `CRC32`
  * If `0xFF` does not occur, `MASK = 0`
  * Mark `0x00` and `0x01` in the map
  * Choose a symbol `x` that does not occur (since len ≤ 249, such a symbol always exists)
  * Compute `MASK = x ^ 0xFF`

* If BEGIN appears again, terminate the current packet and start a new one
* If STOP appears, terminate the packet and consider the link available
* All bytes outside packets are ignored
* If a device has multiple packets and their total size does not exceed a limit, it may send them consecutively separated by BEGIN, with END only at the end of the last packet

---

## 3. Network Layer

* Device has an 8-bit address assigned statically by the user during registration
* Device has a user-defined name (UTF-8 string)
* Device has a variable-length unique ID from hardware (e.g., microcontroller ID). First byte indicates manufacturer
* Router maintains an address map assigning addresses to physical ports:
  * 0 – device unknown
  * 1 – device reachable via port 1
  * 2 – device reachable via port 2
  * Can be stored in 2 bits → 256 * 2 bits = 64 bytes
  * Or compressed to 51 bytes using base-3 encoding (mod/div by 3), skipping address `0x00`

* Each packet starts with source address and one or more destination addresses
* When a router receives a packet, it updates its map based on the source address
* If destination is unknown, packet is broadcast to all ports except the incoming one
* Map may be stored in non-volatile memory when updated
* Destination address `0x00` means a new device without an assigned address
* Router always treats `0x00` as unknown (broadcast)
* Address `0xFF` should be avoided to reduce encoding needs in lower layers (but not forbidden)
* Empty destination list means broadcast
* On startup, a device sends an empty broadcast packet to register itself (possibly multiple times at random intervals)
* If no broadcast was sent for a long time, device sends one again

* Frame:
  ```
  |   1   |  1  | 0..N  |  M   |
  | FLAGS | SRC | DST[] | DATA |

  FLAGS:
     |   7  |  6 5 4   |  3 2 1 0  |
     | zero | PROTOCOL | DST_COUNT |

  0 - Network Management Protocol
  1 - Application Layer Protocol
  2 - Bootloader Protocol
  ```

---

### Network Management Protocol

#### DISCOVERY
* User sends DISCOVERY (broadcast)
* Each device responds with DISCOVERY_RESPONSE after random delay (0–5 s)
* Router also responds
* If router forwards response, it adds its address to the list
* Entry contains router address and port number
* If router is the source, it does not add itself
* Response includes port number from which DISCOVERY came (0 for end device)
* Includes device ID and name
* User application should repeat discovery multiple times to reduce packet loss probability
* Only one discovery process should be active at a time

```
DISCOVERY (broadcast):
  |    1     |   1   |
  | Type = 0 | flags |

  flags:
    bit 0: response with broadcast
    bit 1: forget routing map (routers only)
```

```
DISCOVERY_RESPONSE (unicast to SRC or broadcast):
  |    1     |  1   |  1    | ID len |    1     | namelen |   2 * N   |
  | Type = 1 | port |ID len |   ID   | Name len |  NAME   | routers[] |
```

---

#### SET_ADDRESS
* Assign or change device address
* User sends SET_ADDRESS (broadcast) with ID and new address
* Matching device sets its address
* Responds on success (retry if no response)
* Response uses new address as SRC
* After change, discovery should be run with flags to refresh routing maps

```
SET_ADDRESS (broadcast):
  |     1      |    1    |   1    | ID len |
  | Type = 2   | address | ID len |   ID   |
```

```
SET_ADDRESS_RESPONSE (unicast to SRC):
  |    1     |    1    |   1    | ID len |
  | Type = 3 | address | ID len |   ID   |
```

---

#### SET_NAME
* Change device name

```
SET_NAME (unicast):
  |     1      |      1    | name len |
  | Type = 4   |  name len |   NAME   |
```

```
SET_NAME_RESPONSE (unicast to SRC):
  |    1     |      1    | name len |
  | Type = 5 |  name len |   NAME   |
```

---

#### PACKET_LOST
* Router notifies source about undelivered packet

```
PACKET_LOST (unicast to SRC):
  |     1      |
  | Type = 6   |
```

---

## 4. Application Layer

### Shared object types

#### Data (device state)
* bool, int, etc.
* Device has an array of state fields
* Each field has bit offset, size, and type
* When a field changes, device broadcasts full state
* Less critical data may be delayed or piggybacked
* State is broadcast periodically
* Devices adjust their timing slightly to avoid collisions with neighbors

#### Signals
* Device has signal table
* Other device sends unicast signal
* Receiver responds with ACK
* If no ACK, sender retries
* Signal includes counter to avoid duplicates
* May include parameters

#### Events
* Broadcast events
* Implemented as counters in state table
* Change triggers event
* First received state does not generate event

---

### Name resolution

* Data and signals identified by unique names (UTF-8, may contain dots)
* Device has export table with:
  * type (data/signal)
  * position
  * type and bit size
* Table is persistent across reboot but changes with firmware updates
* Table has unique ID incremented on each update
  * Included in every data/signal packet
  * Mismatch triggers re-resolution or error

* Import process:
  * Device sends broadcast with object name
  * Owner responds with details + table ID

* Imported data may be cached in non-volatile memory

---

```
IMPORT (broadcast):
  |     1      |    1      | name len |
  | Type = 1   |  name len |   NAME   |
```

```
IMPORT_BY_INDEX (unicast):
  |     1      |    1    |
  | Type = 2   |  index  |
```

```
EXPORT (unicast to SRC):
  |    1     |    1    |     1    | name len |    1    |  1   | ...
  | Type = 3 |  index  | name len |   NAME   | tableId | kind | object details ...
```

```
STATE (broadcast):
  |    1     |   1     |      N     |
  | Type = 4 | tableId | state data |
```

```
SIGNAL (unicast):
  |    1     |   1     | 1  |    1     | ...
  | Type = 5 | tableId | id | counter  | signal params ...
```

```
SIGNAL_ACK (unicast to SRC):
  |    1     | 1  |    1     |    1   |
  | Type = 6 | id | counter  | status |
                               ^- 0 - OK, 1 - tableId mismatch
```

---

## Bootloader Protocol

* Used by a constrained bootloader (no DMA, IRQ – polling only)
* Implements:
  * Physical Layer (possibly simplified)
  * Data Link Layer
  * Part of Network Layer
* No Network Management Protocol → no address or name
* Address is always 0, identified by HW ID
* All packets to bootloader are broadcast
* Bootloader may stop receiving during flashing → sender must handle this
* Validates application via first 4 bytes (must be valid stack pointer)
* Programmer should erase first page first, and write first 4 bytes last
* Bootloader is started only from application → app must listen for BOOT packet
* Bootloader update:
  * Send application that overwrites bootloader
  * Then marks itself incomplete (erase first page) so bootloader starts

---

```
QUERY (broadcast):
  |    1     |
  | Type = 1 |
```

```
RESPONSE (unicast):
  |    1     |   1    | ID len | ...
  | Type = 2 | ID len |   ID   | device info
```

```
ERASE (broadcast):
  |    1     |   1    | ID len |    4    |
  | Type = 3 | ID len |   ID   | address |
```

```
WRITE (broadcast):
  |    1     |   1    | ID len |    4    | ...  |
  | Type = 5 | ID len |   ID   | address | data |
```

```
READ (broadcast):
  |    1     |   1    | ID len |    4    |  1  |
  | Type = 7 | ID len |   ID   | address | len |
```

```
RESET (broadcast):
  |    1     |   1    | ID len |
  | Type = 9 | ID len |   ID   |
```

```
BOOT (unicast – only handled by application):
  |    1      |
  | Type = 10 |
```
