#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds app/src/main/assets/device-hashes.json from one or more hash-map.json files a capture wrote on a device.

GIBox's capture (debug page: 采集所有角色的 hash) ties each index upload to the one mesh with its index and vertex
counts and keeps it in GIBox/hash-map.json. Android's index data is the PC's byte for byte, the same on every device of
a game build, so the map is shipped with the App: a texture-only mod is then matched on any device without capturing.
Entries are checked (8-digit hash, mesh name, vertices, submesh starts and counts); a hash two inputs tie to different
meshes is left out and reported.

    python3 tools/make_device_hashes.py hash-map.json [more.json ...] [-o out.json]
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "app", "src", "main", "assets", "device-hashes.json")
HEX8 = re.compile(r"^[0-9a-f]{8}$")
KEYS = ("ib", "variant", "character", "component", "renderer", "mesh", "avatar", "build", "vertices", "bones", "submeshes")


def entries(path):
    d = json.load(open(path, encoding="utf-8"))
    if d.get("format") != "gibox-hash-map" or d.get("version") != 1:
        raise SystemExit("%s: not a GIBox hash map" % path)
    for e in d.get("meshes", []):
        ib = str(e.get("ib", "")).lower()
        subs = e.get("submeshes")
        ok = HEX8.match(ib) and e.get("mesh") and isinstance(e.get("vertices"), int) and e["vertices"] > 0 and isinstance(subs, list) and subs \
            and all(isinstance(s, list) and len(s) == 2 and all(isinstance(v, int) and v >= 0 for v in s) for s in subs)
        if not ok:
            print("%s: skipped a malformed entry %r" % (path, e.get("ib")))
            continue
        out = {k: (ib if k == "ib" else e.get(k, 0 if k in ("vertices", "bones") else "")) for k in KEYS}
        if isinstance(e.get("owner"), str) and e["owner"]:
            out["owner"] = e["owner"]  # 1.0.3: an NPC's or monster's object name, for reading only
        yield out


def main():
    args, out = sys.argv[1:], OUT
    if "-o" in args:
        i = args.index("-o")
        out = args[i + 1]
        args = args[:i] + args[i + 2:]
    if not args:
        print(__doc__)
        sys.exit(2)
    merged, conflicts = {}, set()
    for p in args:
        for e in entries(p):
            old = merged.get(e["ib"])
            shape = (e["mesh"], e["vertices"], json.dumps(e["submeshes"]))
            if old and (old["mesh"], old["vertices"], json.dumps(old["submeshes"])) != shape:
                # One index buffer two meshes share (a character's Pupil and an effect's sphere, 1.0.3): the character's
                # is kept when only one of them is a character's; otherwise neither.
                if old["vertices"] == e["vertices"] and bool(old["avatar"]) != bool(e["avatar"]):
                    if e["avatar"]:
                        merged[e["ib"]] = e
                else:
                    conflicts.add(e["ib"])
            elif not old:
                merged[e["ib"]] = e
            else:  # the same mesh: what either knows (PC part, avatar, owner, bones from a capture)
                for k in ("character", "component", "avatar", "renderer", "owner"):
                    if not old.get(k) and e.get(k):
                        old[k] = e[k]
                old["bones"] = old["bones"] or e["bones"]
    for ib in conflicts:
        merged.pop(ib, None)
    body = ",\n".join(json.dumps(merged[k], ensure_ascii=False, separators=(",", ":")) for k in sorted(merged))
    with open(out, "w", encoding="utf-8") as f:
        f.write('{"format":"gibox-hash-map","version":1,"meshes":[\n' + body + "\n]}\n")
    meshes = {(e["mesh"], e["vertices"], json.dumps(e["submeshes"])) for e in merged.values()}
    pc = sum(1 for e in merged.values() if e["character"])
    print("%d hashes (%d meshes, %d tied to a PC part) -> %s; left out for conflicting meshes: %s" % (len(merged), len(meshes), pc, out, ", ".join(sorted(conflicts)) or "none"))


if __name__ == "__main__":
    main()
