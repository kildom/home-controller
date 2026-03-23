import json
import time
import fcntl
import secrets
import threading
from pathlib import Path
from types import SimpleNamespace
from voluptuous import Schema, Required

# ---------- Constants ----------

ADDR_MIN = 0xE0
ADDR_MAX = 0xFE
KEEP_ALIVE_TIMEOUT = 120  # seconds (2 minutes)

ADDR_FILE = Path(__file__).parent.parent / "addr.json"

# ---------- Internal State ----------

# Thread lock for in-process serialization (fcntl handles cross-process)
_lock = threading.Lock()

# ---------- File Helpers ----------

def _read_data(f: object) -> dict:
    f.seek(0)
    content = f.read().strip()
    return json.loads(content) if content else {}


def _write_data(f: object, data: dict) -> None:
    f.seek(0)
    f.truncate()
    json.dump(data, f, indent=4)


def _addr_key(addr_int: int) -> str:
    return str(addr_int)


# ---------- alloc_address ----------

def alloc_address(token: "str | None" = None) -> "tuple[int, str] | None":
    """
    Allocate a free address.  Call this when a user connects.
    If `token` matches an existing entry in addr.json, the same address is
    re-assigned and the existing token is returned.  Otherwise a new address
    and a fresh random token are allocated.
    Returns (address, token) or None if all addresses are occupied by
    recently active users.
    """
    with _lock:
        ADDR_FILE.touch(exist_ok=True)
        with open(ADDR_FILE, "r+") as f:
            fcntl.flock(f, fcntl.LOCK_EX)
            try:
                data = _read_data(f)
                now = time.time()

                # Re-assign address if token is recognised
                if token is not None:
                    for addr, entry in data.items():
                        if isinstance(entry, dict) and entry.get("token") == token:
                            data[addr]["ts"] = now
                            _write_data(f, data)
                            return (int(addr), token)

                all_addrs = list(range(ADDR_MIN, ADDR_MAX + 1))

                # Prefer a completely free slot first
                free = [a for a in all_addrs if _addr_key(a) not in data]
                if free:
                    chosen = free[0]
                else:
                    # Reclaim the address with the oldest keep-alive if expired
                    oldest = min(data, key=lambda k: data[k]["ts"])
                    if now - data[oldest]["ts"] > KEEP_ALIVE_TIMEOUT:
                        chosen = int(oldest)
                    else:
                        return None

                new_token = secrets.token_hex(16)  # 32 hex characters
                data[_addr_key(chosen)] = {"ts": now, "token": new_token}
                _write_data(f, data)
            finally:
                fcntl.flock(f, fcntl.LOCK_UN)

        return (chosen, new_token)


# ---------- on_disconnect helper ----------

def on_disconnect(session) -> None:
    """
    Call when a websocket disconnects to free resources.
    The file entry expires naturally via the keep-alive timeout.
    """
    session.address = None


# ---------- Schemas ----------

KeepAliveSchema = Schema({
    Required("type"): "keep_alive",
}, extra=False)

ReleaseAddrSchema = Schema({
    Required("type"): "release_addr",
}, extra=False)


# ---------- Command Handlers ----------

async def keep_alive(session, message: SimpleNamespace):
    """Update the keep-alive timestamp for the calling user's address."""
    addr = session.address
    if addr is None:
        return {"type": "keep_alive_result", "success": False,
                "message": "No address assigned"}

    with _lock:
        ADDR_FILE.touch(exist_ok=True)
        with open(ADDR_FILE, "r+") as f:
            fcntl.flock(f, fcntl.LOCK_EX)
            try:
                data = _read_data(f)
                key = _addr_key(addr)
                if key in data:
                    data[key]["ts"] = time.time()
                _write_data(f, data)
            finally:
                fcntl.flock(f, fcntl.LOCK_UN)

    return {"type": "keep_alive_result", "success": True}


async def release_addr(session, message: SimpleNamespace):
    """Release the address assigned to the calling user."""
    addr = session.address
    session.address = None
    if addr is None:
        return {"type": "release_addr_result", "success": False,
                "message": "No address assigned"}

    with _lock:
        if ADDR_FILE.exists():
            with open(ADDR_FILE, "r+") as f:
                fcntl.flock(f, fcntl.LOCK_EX)
                try:
                    data = _read_data(f)
                    data.pop(_addr_key(addr), None)
                    _write_data(f, data)
                finally:
                    fcntl.flock(f, fcntl.LOCK_UN)

    return {"type": "release_addr_result", "success": True}


