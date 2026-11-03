"""Tiny client for the UE 5.8 in-editor MCP server (127.0.0.1:8000/mcp).

Usage from command line:  python _mcp_client.py <json-command-file>
The command file holds a JSON dict:
  {"tool": "call_tool", "args": {...}}
or {"toolsets": true} to list toolsets, or {"describe": "<toolset>"}.
"""
import json
import sys
import urllib.request

URL = "http://127.0.0.1:8000/mcp"
_session = {"id": None}


def rpc(method, params=None, notify=False):
    payload = {"jsonrpc": "2.0", "method": method}
    if not notify:
        payload["id"] = 1
    if params is not None:
        payload["params"] = params
    data = json.dumps(payload).encode()
    req = urllib.request.Request(URL, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    req.add_header("Accept", "application/json, text/event-stream")
    if _session["id"]:
        req.add_header("Mcp-Session-Id", _session["id"])
    with urllib.request.urlopen(req, timeout=600) as resp:
        sid = resp.headers.get("Mcp-Session-Id")
        if sid:
            _session["id"] = sid
        raw = resp.read().decode("utf-8", "replace")
    if notify or not raw.strip():
        return None
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        for line in raw.splitlines():
            line = line.strip()
            if line.startswith("{"):
                try:
                    return json.loads(line)
                except json.JSONDecodeError:
                    continue
    return {"raw": raw}


rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                   "clientInfo": {"name": "qoder", "version": "1"}})
rpc("notifications/initialized", {}, notify=True)


def call(name, args):
    return rpc("tools/call", {"name": name, "arguments": args})


if __name__ == "__main__":
    payload = json.loads(open(sys.argv[1], encoding="utf-8").read())
    if payload.get("top"):
        out = rpc("tools/list", {})
    elif payload.get("toolsets"):
        out = call("list_toolsets", {})
    elif payload.get("describe"):
        out = call("describe_toolset", {"toolset_name": payload["describe"]})
    else:
        out = call(payload["tool"], payload.get("args", {}))
    print(json.dumps(out, ensure_ascii=False, indent=2))
