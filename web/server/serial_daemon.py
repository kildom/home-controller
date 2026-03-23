import json
import asyncio
import base64
import threading
from pathlib import Path
from types import SimpleNamespace
from voluptuous import Schema, Required, All, Match
import serial
import serial.tools.list_ports

# ---------- Daemon State ----------

_daemon_instance = None
_lock = threading.Lock()
_data_received_callbacks = []


# ---------- Schema ----------

SendSchema = Schema({
    Required("type"): "send",
    Required("data"): All(str, Match(r'^[A-Za-z0-9+/]*={0,2}$')),  # Base64 pattern
}, extra=False)


# ---------- Serial Daemon Class ----------

class SerialDaemon:
    def __init__(self):
        self.port = None
        self.serial_port = None
        self.tx_queue = asyncio.Queue()
        self.rx_task = None
        self.tx_task = None
        self.running = False

    def _load_config(self):
        """Load port configuration from config.json"""
        config_path = Path(__file__).parent.parent / "config.json"
        try:
            with open(config_path, 'r') as f:
                config = json.load(f)
            self.port = config.get("port")
            if not self.port:
                raise ValueError("'port' field missing in config.json")
        except FileNotFoundError:
            raise Exception(f"Config file not found: {config_path}")
        except Exception as e:
            raise Exception(f"Failed to load config: {e}")

    def _open_serial(self):
        """Open and configure serial port"""
        try:
            self.serial_port = serial.Serial(
                port=self.port,
                baudrate=115200,
                parity=serial.PARITY_NONE,
                bytesize=serial.EIGHTBITS,
                stopbits=serial.STOPBITS_ONE,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
                timeout=0.1
            )
        except Exception as e:
            raise Exception(f"Failed to open serial port {self.port}: {e}")

    async def _rx_loop(self):
        """Receive loop: read from serial port and pass to main"""
        loop = asyncio.get_event_loop()
        try:
            while self.running:
                def read_serial():
                    if self.serial_port and self.serial_port.in_waiting:
                        return self.serial_port.read(self.serial_port.in_waiting)
                    return None

                data = await loop.run_in_executor(None, read_serial)
                if data and _data_received_callbacks:
                    # Call every registered callback; one failing callback must not block others.
                    for callback in list(_data_received_callbacks):
                        try:
                            await callback(data)
                        except Exception as callback_error:
                            print(f"Error in serial receive callback: {callback_error}")
                await asyncio.sleep(0.01)
        except Exception as e:
            print(f"Error in serial rx loop: {e}")

    async def _tx_loop(self):
        """Transmit loop: send queued data over serial port"""
        try:
            while self.running:
                try:
                    data = await asyncio.wait_for(self.tx_queue.get(), timeout=0.1)
                    loop = asyncio.get_event_loop()
                    await loop.run_in_executor(None, self.serial_port.write, data)
                except asyncio.TimeoutError:
                    pass
        except Exception as e:
            print(f"Error in serial tx loop: {e}")

    async def start(self):
        """Start the serial daemon"""
        global _daemon_instance
        with _lock:
            if self.running:
                raise Exception("Serial daemon is already running")
            
            self._load_config()
            self._open_serial()
            self.running = True
            self.rx_task = asyncio.create_task(self._rx_loop())
            self.tx_task = asyncio.create_task(self._tx_loop())
            _daemon_instance = self

    async def stop(self):
        """Stop the serial daemon"""
        global _daemon_instance
        with _lock:
            if not self.running:
                return
            
            self.running = False
            
            if self.rx_task:
                self.rx_task.cancel()
                try:
                    await self.rx_task
                except asyncio.CancelledError:
                    pass
                self.rx_task = None
            
            if self.tx_task:
                self.tx_task.cancel()
                try:
                    await self.tx_task
                except asyncio.CancelledError:
                    pass
                self.tx_task = None
            
            if self.serial_port:
                self.serial_port.close()
                self.serial_port = None
            
            _daemon_instance = None

    async def send_data(self, data_b64: str):
        """Queue data to send over serial port"""
        try:
            data_bytes = base64.b64decode(data_b64)
            await self.tx_queue.put(data_bytes)
            return {"type": "send_result", "success": True}
        except Exception as e:
            return {"type": "send_result", "success": False, "message": str(e)}

    async def send_raw_data(self, data: bytes):
        """Queue raw bytes to send over serial port"""
        await self.tx_queue.put(bytes(data))


# ---------- Global Functions ----------

def set_data_received_callback(callback):
    """Register a callback for received data.

    Multiple callbacks can be registered at the same time.
    """
    if callback not in _data_received_callbacks:
        _data_received_callbacks.append(callback)


def remove_data_received_callback(callback):
    """Unregister a previously registered callback for received data."""
    if callback in _data_received_callbacks:
        _data_received_callbacks.remove(callback)


async def start():
    """Start the serial daemon singleton"""
    daemon = SerialDaemon()
    await daemon.start()


async def stop():
    """Stop the serial daemon singleton"""
    global _daemon_instance
    if _daemon_instance:
        await _daemon_instance.stop()


async def send(session, message: SimpleNamespace):
    """Command handler: send data over serial port"""
    global _daemon_instance
    if not _daemon_instance or not _daemon_instance.running:
        return {"type": "send_result", "success": False, "message": "Serial daemon not running"}

    return await _daemon_instance.send_data(message.data)


async def send_raw(data: bytes):
    """Send raw bytes over serial port."""
    global _daemon_instance
    if not _daemon_instance or not _daemon_instance.running:
        raise Exception("Serial daemon not running")

    await _daemon_instance.send_raw_data(data)

