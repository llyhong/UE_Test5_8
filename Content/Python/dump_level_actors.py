"""Dump every actor in the open level (label / class / transform) via the in-editor MCP server.

Writes incrementally to %TEMP%/actors_dump.json so partial progress survives a timeout.
"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _mcp_client as mc

OUT = os.path.join(os.environ.get("TEMP", "."), "actors_dump.json")
SCENE = "editor_toolset.toolsets.scene.SceneTools"
ACTOR = "editor_toolset.toolsets.actor.ActorTools"
OBJ = "editor_toolset.toolsets.object.ObjectTools"


def unwrap(resp):
    try:
        text = resp["result"]["content"][0]["text"]
        return json.loads(text)
    except Exception:
        return resp


def tool(toolset, name, args):
    r = unwrap(mc.call("call_tool", {
        "toolset_name": toolset,
        "tool_name": name,
        "arguments": args,
    }))
    if isinstance(r, dict) and "returnValue" in r:
        return r["returnValue"]
    if isinstance(r, dict) and r.get("isError"):
        raise RuntimeError(str(r)[:200])
    return r


def save(rows, done):
    tmp = OUT + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False)
    os.replace(tmp, OUT)
    print("saved %d/%d" % (len(rows), done), flush=True)


def main():
    t0 = time.time()
    actors = tool(SCENE, "find_actors", {"name": "", "tag": "", "collision_channels": []})
    print("actors:", len(actors), flush=True)

    rows = []
    for i, a in enumerate(actors):
        rec = {"ref": a, "label": "", "cls": "", "x": 0.0, "y": 0.0, "z": 0.0,
               "yaw": 0.0, "pitch": 0.0, "roll": 0.0, "sx": 1.0, "sy": 1.0, "sz": 1.0}
        try:
            rec["label"] = tool(ACTOR, "get_label", {"actor": a}) or ""
        except Exception as e:
            rec["label"] = "<ERR:%s>" % str(e)[:40]
        try:
            xf = tool(ACTOR, "get_actor_transform", {"actor": a}) or {}
            loc = xf.get("location") or {}
            rot = xf.get("rotation") or {}
            scl = xf.get("scale") or {}
            rec.update(
                x=round(float(loc.get("x", 0.0)), 1),
                y=round(float(loc.get("y", 0.0)), 1),
                z=round(float(loc.get("z", 0.0)), 1),
                yaw=round(float(rot.get("yaw", 0.0)), 1),
                pitch=round(float(rot.get("pitch", 0.0)), 1),
                roll=round(float(rot.get("roll", 0.0)), 1),
                sx=round(float(scl.get("x", 1.0)), 3),
                sy=round(float(scl.get("y", 1.0)), 3),
                sz=round(float(scl.get("z", 1.0)), 3),
            )
        except Exception as e:
            rec["label"] += " <XFERR:%s>" % str(e)[:40]
        try:
            cls = tool(OBJ, "get_class", {"instance": a}) or {}
            rec["cls"] = str(cls.get("refPath", "")).split(".")[-1]
        except Exception:
            rec["cls"] = "?"
        rows.append(rec)
        if (i + 1) % 40 == 0:
            save(rows, len(actors))
            print("  %.1fs" % (time.time() - t0), flush=True)

    save(rows, len(actors))
    print("DONE %d actors in %.1fs -> %s" % (len(rows), time.time() - t0, OUT), flush=True)


if __name__ == "__main__":
    main()
