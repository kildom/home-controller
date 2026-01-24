

import json
from pathlib import Path
from pydantic import BaseModel, Field, constr
from typing import Literal

'''

set_auth


'''

class SetAuthModelResult(BaseModel):
    type: Literal["set_auth_result"]
    success: bool

class CommandModel(BaseModel):
    type: str

class SetAuthModel(CommandModel):
    type: Literal["set_auth"] 
    class Auth(BaseModel):
        x: str = Field(..., pattern=r'^[0-9a-fA-F]{64}$')
        y: str = Field(..., pattern=r'^[0-9a-fA-F]{64}$')
        salt: str = Field(..., pattern=r'^[0-9a-fA-F]{64}$')
        ch1: str = Field(..., pattern=r'^[0-9a-fA-F]{64}$')
    auth: Auth

async def set_auth(websocket, message: SetAuthModel):
    with open(Path(__file__).parent.parent / "client/static/auth.json", 'w') as f:
        json.dump(message.auth.model_dump(), f, indent=4)
    return SetAuthModelResult(
        type="set_auth_result",
        success=True
    )

commands = {
    "set_auth": (set_auth, SetAuthModel),
}

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
    command_func, CommandModel = commands[command]
    try:
        validated_message = CommandModel(**message)
    except Exception as e:
        await websocket.send(json.dumps({"type": "error", "message": f"Invalid message format for command {command}", "details": str(e)}))
        return
    try:
        res = await command_func(websocket, validated_message)
    except Exception as e:
        await websocket.send(json.dumps({"type": "error", "message": f"Error executing command {command}: {str(e)}"}))
        return
    if res is not None:
        if isinstance(res, BaseModel):
            res = res.model_dump()
        await websocket.send(json.dumps(res))
