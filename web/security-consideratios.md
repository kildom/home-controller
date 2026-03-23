# Security Considerations Review

Date: 2026-03-23
Scope: Password-based authentication and cryptography in client/server implementation.

## Executive Summary

The authentication flow is functional, but it is not strong enough against realistic attacks in its current form.
Main concerns are offline password cracking, replayability of the phase-1 cookie signature, and insecure transport acceptance (HTTP/WS).

## Findings

### 1. Critical: Offline Password Cracking Is Feasible

- The server exposes `auth.json` containing `x`, `y`, and `salt`.
- The client derives EC private scalar deterministically from `PBKDF2(password, salt, 100000)`.
- An attacker can perform unlimited offline guesses: derive candidate key -> compute public key -> compare to `(x, y)`.
- This bypasses online rate-limiting entirely.

Relevant code:
- `server/serve_file.py` (serves `auth.json`)
- `auth.json`
- `client/src/crypto.ts` (PBKDF2 and deterministic key derivation)

### 2. High: Phase-1 Cookie Is Replayable Indefinitely

- Cookie `connectKey` is a signature over static `ch1`.
- Server accepts any valid signature over `ch1`; no nonce freshness, expiry, or one-time use tracking.
- If cookie leaks once, it can be replayed repeatedly until credentials rotate.

Relevant code:
- `server/main.py` (cookie extraction and static challenge verification)
- `client/src/connection.ts` (sets `connectKey` from JS)

### 3. High: No Mandatory TLS (HTTP/WS Allowed)

- Server and client permit operation over plaintext transport.
- In non-TLS deployments, traffic and cookies can be sniffed/modified.
- This directly amplifies replay risk.

Relevant code:
- `server/main.py` (plain `serve(...)`)
- `client/src/connection.ts` (protocol conversion `http->ws`, `https->wss`)

### 4. Medium: Sensitive Data Is Logged in Browser Console

- Password and derived key material are logged.
- Signature/cookie material is logged.
- Hardcoded password attempt (`"1234"`) exists in login flow.

Relevant code:
- `client/src/connection.ts` (debug logs)
- `client/src/main.tsx` (default password attempt)

### 5. Medium: Any Authenticated Session Can Replace Global Auth

- `set_auth` updates `auth.json` immediately.
- No additional privilege boundary or step-up authentication.
- Any compromised authenticated client can rotate credentials and lock out legitimate users.

Relevant code:
- `server/commands.py` (`set_auth` handler and command registration)

### 6. Low-Medium: Missing Origin Validation for WebSocket Handshake

- No explicit `Origin` allowlist check in WebSocket request handling.
- Increases attack surface in browser-mediated scenarios.

Relevant code:
- `server/main.py` (`process_request`)

## What Looks Correct

- Phase-2 challenge is fresh (`os.urandom(32)`) and signed per connection.
- ECDSA verification includes range checks and curve/order validation.

Relevant code:
- `server/main.py` (phase-2 challenge and verification)
- `server/ecdsa.py` (signature verification safeguards)

## Overall Security Verdict

Current design is not sufficient for strong password-based security against determined attackers.
It is cryptographically coherent at the primitive level, but protocol-level choices introduce high-impact weaknesses.

## Recommendations (Password-Based, No 2FA Requirement)

1. Replace deterministic password->EC-key derivation with a PAKE (for example OPAQUE or SRP) to prevent offline verifier cracking.
2. Remove static phase-1 cookie challenge, or make it one-time and short-lived with server-side nonce tracking.
3. Enforce HTTPS/WSS in deployment and reject insecure transport/origins.
4. Remove logs containing passwords, key material, signatures, or tokens.
5. Protect `set_auth` with stronger authorization controls (step-up auth and anti-replay).
6. Add Origin allowlist checks for WebSocket upgrade path.

## Notes

- This report is a static code/protocol review.
- No dynamic penetration test or fuzzing was run in this pass.
