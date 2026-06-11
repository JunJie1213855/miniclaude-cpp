#!/usr/bin/env python3
"""Minimal MCP JSON-RPC test server over stdio.

Supports: initialize, tools/list, tools/call (echo + add), notifications/initialized.
Reads one JSON-RPC line from stdin, writes one JSON-RPC line to stdout.
"""

import json
import sys


def handle_initialize(req_id, params):
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {
            "protocolVersion": "2024-11-05",
            "serverInfo": {"name": "test-mcp-server", "version": "0.1.0"},
            "capabilities": {"tools": {}},
        },
    }


def handle_tools_list(req_id, params):
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {
            "tools": [
                {
                    "name": "echo",
                    "description": "Echo back the message argument",
                    "inputSchema": {
                        "type": "object",
                        "properties": {
                            "message": {"type": "string", "description": "Text to echo"}
                        },
                        "required": ["message"],
                    },
                },
                {
                    "name": "add",
                    "description": "Add two numbers",
                    "inputSchema": {
                        "type": "object",
                        "properties": {
                            "a": {"type": "number", "description": "First number"},
                            "b": {"type": "number", "description": "Second number"},
                        },
                        "required": ["a", "b"],
                    },
                },
            ]
        },
    }


def handle_tools_call(req_id, params):
    name = params.get("name", "")
    args = params.get("arguments", {})

    if name == "echo":
        msg = args.get("message", "")
        text = f"echo: {msg}"
    elif name == "add":
        a = args.get("a", 0)
        b = args.get("b", 0)
        text = str(a + b)
    else:
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "error": {"code": -32601, "message": f"Unknown tool: {name}"},
        }

    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "result": {"content": [{"type": "text", "text": text}]},
    }


def write_response(resp):
    sys.stdout.write(json.dumps(resp) + "\n")
    sys.stdout.flush()


def main():
    handlers = {
        "initialize": handle_initialize,
        "tools/list": handle_tools_list,
        "tools/call": handle_tools_call,
    }

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            continue

        method = req.get("method", "")

        # Notification — no response needed
        if "id" not in req:
            if method == "notifications/initialized":
                pass  # server accepts, no reply
            continue

        req_id = req["id"]
        params = req.get("params", {})

        handler = handlers.get(method)
        if handler:
            resp = handler(req_id, params)
        else:
            resp = {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {"code": -32601, "message": f"Method not found: {method}"},
            }
        write_response(resp)


if __name__ == "__main__":
    main()
