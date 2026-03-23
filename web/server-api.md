# Home Controller — WebSocket Server API

## Overview

The server exposes a single combined HTTP + WebSocket endpoint on port `8001` (configurable via `PORT` environment variable). Static files are served over HTTP; the WebSocket connection is available at `/connect`. All WebSocket messages are JSON-encoded text frames.

---

## Authentication

Authentication is **two-phase**. Phase 1 uses an HTTP cookie to gate the WebSocket upgrade. Phase 2 uses a WebSocket challenge-response handshake after the connection is upgraded.

### Cryptographic Primitives

All signatures use **ECDSA over P-256 (secp256r1) with SHA-256**. The client's private key `d` is derived deterministically from a user password using PBKDF2:

1. Encode the password as UTF-8.
2. Run **PBKDF2** with SHA-256, a 32-byte random salt, and **100 000 iterations** to produce a 256-bit key.
3. Export the raw key bytes — these bytes `d` are the ECDSA private scalar.
4. If `d` begins with `0xFFFFFFFF` (reserved range), regenerate with a new salt.
5. Compute the corresponding public key point `(x, y) = d · G` on P-256.

The persistent auth configuration is stored in `auth.json` (server-side) and `/auth.json` (served statically to the client):

```json
{
    "x":    "<64 hex chars — P-256 public key X coordinate>",
    "y":    "<64 hex chars — P-256 public key Y coordinate>",
    "salt": "<64 hex chars — PBKDF2 salt used to derive d>",
    "ch1":  "<64 hex chars — static challenge for cookie auth>"
}
```

---

### Phase 1 — Cookie-Based Pre-Authentication (HTTP Upgrade Gate)

Before the WebSocket upgrade is allowed, the server validates an HTTP cookie.

**Client steps:**

1. Fetch `/auth.json` to obtain `ch1`, `x`, `y`, `salt`.
2. Derive the private key `d` from the password using the stored `salt`.
3. Sign the raw bytes of `ch1` (decoded from hex, 32 bytes) with ECDSA P-256 / SHA-256 using `d`.
4. Encode the signature as a 128-character lowercase hex string (64 bytes: `r || s` in the format produced by the WebCrypto `sign` API for P-256).
5. Set the HTTP cookie before initiating the WebSocket connection:

```
Cookie: connectKey=<128-hex-char signature of ch1>
```

**Server validation:**

- Extracts `connectKey` from the `Cookie` header using the regex `connectKey=([0-9a-fA-F]{128})`.
- Reads `ch1`, `x`, `y` from `auth.json`.
- Splits the 128-char hex into `r = sig[:64]`, `s = sig[64:]`.
- Verifies the signature against `ch1` bytes using the P-256 public key `(x, y)`.
- Returns **403 Forbidden** if validation fails; allows the websocket upgrade if it passes.

---

### Phase 2 — WebSocket Challenge-Response Handshake

Immediately after the WebSocket connection is upgraded, the server initiates a fresh challenge:

**Step 1 — Server sends challenge:**

```json
{
    "type": "challenge",
    "challenge": "<64 hex chars — 32 random bytes>"
}
```

**Step 2 — Client responds:**

The client signs the raw bytes of `challenge` (decoded from hex) with ECDSA P-256 / SHA-256 using the same private key `d`.

```json
{
    "type": "challenge_response",
    "signature": "<128 hex chars — ECDSA signature r||s>",
    "address_token": "<32 hex chars — optional, from previous session>"
}
```

- `signature`: 128 hex chars (64 bytes, `r || s`).
- `address_token`: optional. If provided and recognised, the server re-assigns the same logical address from the previous session.

**Step 3a — Server accepts (success):**

```json
{
    "type": "challenge_accepted",
    "address": 224,
    "token": "<32 hex chars>"
}
```

- `address`: integer in range `0xE0`–`0xFE` (224–254) — the logical device address assigned to this session.
- `token`: 32 hex chars, opaque session token. The client should persist this (e.g. in `sessionStorage`) and send it as `address_token` on the next reconnect to reclaim the same address.

**Step 3b — Server rejects (failure):**

```json
{
    "type": "error",
    "message": "..."
}
```

The connection is closed immediately after a rejection.

---

### Complete Connection Sequence

```
Client                                    Server
  |                                          |
  |--- HTTP GET /auth.json ----------------> |
  |<-- 200 {x,y,salt,ch1} -------------------|
  |                                          |
  |--- HTTP GET /connect                     |
  |    Cookie: connectKey=<sig1 of ch1>  --> |
  |                                          | (verifies sig1; 403 if invalid)
  |<-- 101 Switching Protocols --------------|
  |                                          |
  |<-- {"type":"challenge","challenge":"ch2"}|
  |                                          |
  |--- {"type":"challenge_response",         |
  |     "signature":"<sig2 of ch2>",         |
  |     "address_token":"<token>"}       --> |
  |                                          | (verifies sig2; allocates address)
  |<-- {"type":"challenge_accepted",         |
  |     "address":224,"token":"..."}         |
  |                                          |
  |        [session active]                  |
```

---

## Address Allocation

- The server maintains a pool of **31 addresses** in the range `0xE0`–`0xFE` (224–254).
- Each address entry records a timestamp (last keep-alive) and an opaque token.
- On connect, if the client provides a known `address_token`, the same address is re-assigned.
- Otherwise, the first free slot is allocated. If all slots are occupied, the address with the oldest timestamp is reclaimed — but only if it has been idle for more than **120 seconds**.
- If no address can be allocated, the server responds with an error and closes the connection.
- On disconnect, the address entry is left in place and expires naturally after the 120-second keep-alive timeout.

---

## Post-Authentication Commands

All messages after `challenge_accepted` are JSON objects with a `"type"` field. Invalid JSON, missing `"type"`, unknown commands, or schema violations result in an `error` response.

---

### `keep_alive`

Refreshes the keep-alive timestamp for the session's address. Must be sent at least once every **120 seconds** to prevent the address from being reclaimed.

**Request:**
```json
{ "type": "keep_alive" }
```

**Response:**
```json
{ "type": "keep_alive_result", "success": true }
```

On failure (no address assigned):
```json
{ "type": "keep_alive_result", "success": false, "message": "No address assigned" }
```

---

### `release_addr`

Explicitly releases the session's assigned address, making it immediately available for other clients.

**Request:**
```json
{ "type": "release_addr" }
```

**Response:**
```json
{ "type": "release_addr_result", "success": true }
```

On failure (no address assigned):
```json
{ "type": "release_addr_result", "success": false, "message": "No address assigned" }
```

---

### `send`

Sends raw bytes to the serial port (and thus to the hardware bus).

**Request:**
```json
{
    "type": "send",
    "data": "<standard Base64-encoded bytes>"
}
```

- `data`: standard Base64 string (characters `A-Z`, `a-z`, `0-9`, `+`, `/`, with `=` padding).

**Response:**
```json
{ "type": "send_result", "success": true }
```

On failure:
```json
{ "type": "send_result", "success": false, "message": "..." }
```

---

### `set_auth`

Replaces the server's authentication credentials (`auth.json`). This changes the password used for all future connections. The new parameters must be produced by the same PBKDF2 + ECDSA key derivation process.

**Request:**
```json
{
    "type": "set_auth",
    "auth": {
        "x":    "<64 hex chars — new public key X>",
        "y":    "<64 hex chars — new public key Y>",
        "salt": "<64 hex chars — PBKDF2 salt for new password>",
        "ch1":  "<64 hex chars — new static challenge for cookie auth>"
    }
}
```

All four fields must be exactly 64 lowercase or uppercase hexadecimal characters.

**Response:**
```json
{ "type": "set_auth_result", "success": true }
```

---

## Server-Initiated Messages

The server may push messages to all connected clients at any time after authentication.

---

### `recv`

Sent to **all connected sessions** whenever raw bytes are received from the serial port.

```json
{
    "type": "recv",
    "data": "<standard Base64-encoded bytes>"
}
```

- `data`: Base64-encoded raw bytes as received from the hardware bus.

---

### `error`

Sent when a command cannot be processed.

```json
{
    "type": "error",
    "message": "<description>",
    "details": "<optional — schema validation details or exception message>"
}
```

Common causes:
- Invalid JSON payload.
- Missing `"type"` field.
- Unknown command type.
- Schema validation failure (wrong field types, missing required fields, extra fields).
- Internal error during command execution.

---

## Summary Table

### Client → Server

| `type`           | Required fields                          | Optional fields     | Response type        |
|------------------|------------------------------------------|---------------------|----------------------|
| `challenge_response` | `signature` (128 hex)               | `address_token` (32 hex) | `challenge_accepted` or `error` |
| `keep_alive`     | —                                        | —                   | `keep_alive_result`  |
| `release_addr`   | —                                        | —                   | `release_addr_result`|
| `send`           | `data` (Base64)                          | —                   | `send_result`        |
| `set_auth`       | `auth.x`, `auth.y`, `auth.salt`, `auth.ch1` (all 64 hex) | — | `set_auth_result` |

### Server → Client

| `type`               | Fields                          | Trigger                              |
|----------------------|---------------------------------|--------------------------------------|
| `challenge`          | `challenge` (64 hex)            | Sent immediately on WebSocket connect |
| `challenge_accepted` | `address` (int), `token` (32 hex) | Successful authentication          |
| `recv`               | `data` (Base64)                 | Data received from hardware serial port (broadcast to all sessions) |
| `keep_alive_result`  | `success` (bool)[, `message`]   | Response to `keep_alive`             |
| `release_addr_result`| `success` (bool)[, `message`]   | Response to `release_addr`           |
| `send_result`        | `success` (bool)[, `message`]   | Response to `send`                   |
| `set_auth_result`    | `success` (bool)                | Response to `set_auth`               |
| `error`              | `message`[, `details`]          | Invalid command or internal error    |
