# Network Layers

## 1. Physical Layer

* Medium:
  * Half-duplex RS-485  
  * Half-duplex 1-wire UART  
* All devices are equal (no master/slave concept).  
* By default, a device stays in UART listening mode.  
* The link is considered *available* if the last packet has finished, or if nothing has been transmitted for a defined amount of time (used when the last packet is unknown).  
* If a device wants to transmit, it waits for a random time (distribution depends on queue length, wait time of the first queued packet, and highest priority of packets in the queue).  
  * Switch to GPIO mode  
  * Set `DataOutput = 0`  
  * Check one last time that no one else started transmitting (line low)  
  * Set `TxEnable = 1`  
  * Wait 1 Tbit  
  * Set `DataOutput = 1`  
  * Wait ~0.25 Tbit  
  * Set `TxEnable = 0`  
  * Wait 8.75 Tbit  
  * Wait a random delay between 0–10 Tbit (fractional times included)  
  * Repeat this process two more times  
  * If, during that process, another node starts transmitting, abort the attempt, switch back to UART mode, and wait a defined cooldown before considering the line available again.  
  * Switch back to UART mode, `TxEnable = 1`  
  * ```
      <-- 10 --><- 0..10 -><-- 10 --><- 0..10 -><-- 10 -->
    -- ---------........... ---------........... ---------DDDDDDDDDDD
      -                    -                    -         DDDDDDDDDDD
    ```
  * Transmit the packet  
  * Switch to UART mode, `TxEnable = 0`  

  * Practical implementation example (STM32):
    * **RS-485:**
      * Configure TIMER in one-pulse mode  
      * `DataOut = 0`, disable interrupts, check input, `TxEn = 1`, enable TIMER and interrupts  
      * After 1 Tbit: timer sets `DataOut = 1`  
      * After 1.25 Tbit: interrupt sets `TxEn = 0` and switches input to listen for IRQ  
      * After `10 + rand` Tbit: interrupt retries  
      * OR, if all retries are done, after 10 Tbit interrupt starts transmission  
    * **1-wire UART:**
      * Idle: `Tx = Hi-Z`  
      * Configure TIMER in one-pulse mode  
      * Disable interrupts, check input, `Tx = 0`, enable TIMER and interrupts  
      * After 1 Tbit: timer sets `Tx = 1`  
      * After 1.1 Tbit: interrupt sets `Tx = Hi-Z` and switches to listening IRQ  
      * After `10 + rand` Tbit: retry  
      * OR, after 10 Tbit: start transmission  
* If no packet activity is detected for a defined period, the state resets.

---

## 2. Data Link Layer

* Packet format:
  ```
  |  1  |  1   |   len     |     4      |  1  |  1   |      ESC = 0xAA
  | ESC | MASK | DATA^MASK | CRC32^MASK | ESC | STOP |      STOP = 0xFF
  |   BEGIN    |        CONTENT         |    END     |      len = 0..249
  ```
* How to calculate `MASK`:
  * Build a symbol occurrence map for `DATA` and `CRC32`.  
  * If `0xAA` doesn’t occur, then `MASK = 0`.  
  * Mark `0x00` and `0x55` in the map.  
  * Pick a non-occurring symbol `x` (since `len <= 249`, such a symbol always exists).  
  * Compute `MASK = x ^ 0xAA`.  
* If another BEGIN byte (`0xAA`) appears, terminate the current packet and start a new one.  
* If a STOP (`0xFF`) appears, terminate the packet and mark the line as available.  
* All bytes outside a valid packet are ignored.  
* If a device has several packets to send that together fit within a size limit, it can send them back-to-back — separating them with BEGIN markers, and a single END marker at the very end.

---

## 3. Network Layer

* Each device has an 8-bit address assigned statically by the user when registering it in the network.  
* Each device also has a user-defined name (UTF-8 string).  
* Each device has a unique hardware ID of variable length (e.g., MCU hardware ID). The first byte identifies the manufacturer.  
* A router maintains a map of device addresses assigned to physical ports. For example, with ports 1 and 2:
  * `0` – device unknown  
  * `1` – device reachable via port 1  
  * `2` – device reachable via port 2  
  * The map can be stored in 2 bits per entry → 256 × 2 bits = 64 bytes.  
  * Or compressed to 51 bytes (5 addresses per byte, using mod/div by 3). Must also exclude address `0x00`.  
* Each packet starts with a source address and one or more destination addresses.  
* When a router receives a packet, it updates its address map using the source address.  
* If the destination address is unknown, the packet is broadcast to all ports except the one it came from.  
* The map can be saved to non-volatile memory when updated.  
* Destination address `0x00` means a new, unassigned device.  
* The router always treats `0x00` as unknown (i.e., broadcasts).  
* Address `0xAA` should generally be avoided to reduce the need for lower-layer encoding, but it’s still allowed.  
* An empty destination list means broadcast.  
* On startup, a device sends an empty broadcast packet to register itself in the network.  
  It can repeat this a few times at random intervals.  
  If a device hasn’t sent a broadcast for a long time, it should send one again.  

* Frame format:
  ```
  |   1   |  1  | 0..N  |  M   |
  | FLAGS | SRC | DST[] | DATA |

  FLAGS:
     |   7  |  6 5 4   |  3 2 1 0  |
     | zero | PROTOCOL | DST_COUNT |
  0 - discovery protocol
  ```

---

### Discovery Protocol

* **DISCOVERY** – checking which devices are present:
  * The user sends a DISCOVERY packet (broadcast).  
  * Each device responds with a `DISCOVERY_RESPONSE` after a random delay (0–5 seconds).  
  * Routers respond too.  
  * If a router forwards a `DISCOVERY_RESPONSE`, it appends its own address to the list.  
  * The router’s entry includes its address and port number.  
  * If the router is the *source* of the response, it doesn’t add its address again.  
  * Each response includes the port number from which the DISCOVERY came.  
    For end devices, this port is always 0.  
  * The response also includes the device ID and name.  
  * The user application should perform discovery several times to minimize packet loss.  
  * Only one discovery process should run at any given time.  
  ```
  DISCOVERY (broadcast):
    |    1     |   1   |
    | Type = 0 | flags |
    flags:
      bit 0: respond with broadcast  
      bit 1: forget routing map (routers only)

  DISCOVERY_RESPONSE (unicast to SRC or broadcast):
    |    1     |  1   |  1    | ID len |    1     | namelen |   2 * N   |
    | Type = 1 | port |ID len |   ID   | Name len |  NAME   | routers[] |
  ```

* **SET_ADDRESS** – assigning or changing a device address:
  * The user sends a `SET_ADDRESS` packet (broadcast) containing the device ID and the new address.  
  * The device with that ID updates its address.  
  * It responds with success; if not, retry.  
  * The response uses the *new* address in the SRC field.  
  * After changing the address, the app should perform discovery again using flags  
    “forget routing map” and “response with broadcast” to refresh router maps.  
  ```
  SET_ADDRESS (broadcast):
    |     1      |    1    |   1    | ID len |
    | Type = 2   | address | ID len |   ID   |

  SET_ADDRESS_RESPONSE (unicast to SRC):
    |    1     |    1    |   1    | ID len |
    | Type = 3 | address | ID len |   ID   |
  ```

* **SET_NAME** – changing a device’s name:
  * The user sends a `SET_NAME` packet (unicast) with the new name.  
  * The device responds with success; if not, retry.  
  ```
  SET_NAME (unicast):
    |     1      |      1    | name len |
    | Type = 4   |  name len |   NAME   |

  SET_NAME_RESPONSE (unicast to SRC):
    |    1     |      1    | name len |
    | Type = 5 |  name len |   NAME   |
  ```

