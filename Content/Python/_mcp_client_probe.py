import json
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
    with urllib.request.urlopen(req, timeout=15) as resp:
        sid = resp.headers.get("Mcp-Session-Id")
        if sid:
            _session["id"] = sid
        raw = resp.read().decode("utf-8", "replace")
    if notify:
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


init = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                          "clientInfo": {"name": "qoder", "version": "1"}})
print("session", _session["id"])
rpc("notifications/initialized", {}, notify=True)
tools = rpc("tools/list", {}).get("result", {}).get("tools", [])
print("tools:", len(tools))
for t in tools:
    print("-", t["name"], "|", (t.get("description") or "")[:100].replace("\n", " "))
