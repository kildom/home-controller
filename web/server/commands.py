import json
from pathlib import Path
from types import SimpleNamespace
from voluptuous import Schema, Required, All, Match, ALLOW_EXTRA

# ---------- Helpers ----------

def to_ns(obj):
    if isinstance(obj, dict):
        return SimpleNamespace(**{k: to_ns(v) for k, v in obj.items()})
    if isinstance(obj, list):
        return [to_ns(i) for i in obj]
    return obj

def namespace_to_dict(obj):
    if isinstance(obj, SimpleNamespace):
        return {k: namespace_to_dict(v) for k, v in obj.__dict__.items()}
    elif isinstance(obj, list):
        return [namespace_to_dict(i) for i in obj]
    elif isinstance(obj, dict):
        return {k: namespace_to_dict(v) for k, v in obj.items()}
    else:
        return obj

hex64 = Match(r'^[0-9a-fA-F]{64}$')

# ---------- Schemas ----------

SetAuthSchema = Schema({
    Required("type"): "set_auth",
    Required("auth"): {
        Required("x"): All(str, hex64),
        Required("y"): All(str, hex64),
        Required("salt"): All(str, hex64),
        Required("ch1"): All(str, hex64),
    }
}, extra=False)


# ---------- Result Builders ----------

def set_auth_result(success: bool):
    return {
        "type": "set_auth_result",
        "success": success,
    }


# ---------- Command Handlers ----------

async def set_auth(websocket, message: SimpleNamespace):
    with open(Path(__file__).parent.parent / "client/static/auth.json", 'w') as f:
        json.dump(namespace_to_dict(message.auth), f, indent=4)

    return set_auth_result(True)


# ---------- Command Registry ----------

commands = {
    "set_auth": (set_auth, SetAuthSchema),
}


# ---------- Executor ----------

async def execute_command(websocket, message_string):
    try:
        message = json.loads(message_string)
        if not isinstance(message, dict):
            raise ValueError("Message is not a JSON object")
        if "type" not in message:
            raise ValueError("Message missing 'type' field")
    except Exception as e:
        await websocket.send(json.dumps({"type": "error", "message": "Invalid JSON", "details": str(e)}))
        return
    command = str(message.get("type"))
    if command not in commands:
        await websocket.send(json.dumps({"type": "error", "message": f"Unknown command: {command}"}))
        return
    command_func, schema = commands[command]

    # -------- Validation via Voluptuous --------
    try:
        validated_dict = schema(message)  # raises if invalid
        validated_message = to_ns(validated_dict)
    except Exception as e:
        await websocket.send(json.dumps({"type": "error", "message": f"Invalid message format for command {command}", "details": str(e)}))
        return

    # -------- Execute Command --------
    try:
        res = await command_func(websocket, validated_message)
    except Exception as e:
        await websocket.send(json.dumps({"type": "error", "message": f"Error executing command {command}: {str(e)}"}))
        return

    # -------- Send Result --------
    if res is not None:
        await websocket.send(json.dumps(res))
