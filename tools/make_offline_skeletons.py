#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds a skeleton table (one record per skinned mesh) from skinned prefabs exported from the game's files.

The export (renderers_and_bones.json: one prefab per line, each SkinnedMeshRenderer with its mesh and its m_Bones in
their original order, each bone's parent as an index into the same list or -1; bindposes.json: one mesh per line with
its m_BindPose) holds what GIBox records in a bone library (skeleton.json) when a character is shown: per renderer the
bone names in order, their parents and the bind poses. For the original Citlali the two are equal: every bone name,
parent and bind pose of Body, Brow, Face, Face_Eye and three skill-object meshes, bit for bit, once the matrices are
transposed (the export keeps the file's row-major order; skeleton.json holds Unity's column-major memory order).

A mesh is written once per bone list its renderers use (the same model as a character and as an NPC can carry one
more bone on one of them), with the renderer name, the parents and the prefabs using it. Left out and counted: a bone
list that is not one bone per bind pose, a mesh with no bind poses, a renderer with an empty bone reference.

    python3 tools/make_offline_skeletons.py export-folder [-o skeletons.json] [--check skeleton.json ...]
        [--only npc,monster,glider,character]
    python3 tools/make_offline_skeletons.py --self-test
"""
import json
import os
import struct
import sys
from collections import defaultdict

CATEGORIES = {"npc": "npc", "monster": "monster", "glider": "glider", "character": "character"}


def f32(x):
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def column_major(row_major):
    """The export's m00,m01,...,m33 as Unity's in-memory order m00,m10,m20,m30,m01,..."""
    return [f32(row_major[r * 4 + c]) for c in range(4) for r in range(4)]


def lines(path, key):
    """The objects of one top-level array written one per line."""
    with open(path, encoding="utf-8-sig") as f:
        head = f.readline()
        if '"%s":[' % key not in head:
            raise SystemExit("%s: no %s array on the first line" % (path, key))
        for line in f:
            line = line.strip()
            if not line or line == "]}":
                continue
            if line.endswith(","):
                line = line[:-1]
            yield json.loads(line)


def category(prefab):
    c = str(prefab.get("category", "")).lower()
    if c in CATEGORIES:
        return c
    o = str(prefab.get("owner", ""))
    if o.startswith("Flycloak_"):
        return "glider"
    if o.startswith(("Monster_", "MonsterObj_")):
        return "monster"
    if o.startswith("NPC_"):
        return "npc"
    return "character" if o.startswith("Avatar_") else ""


def number(x):
    return format(x, ".9g")


def build(folder, only):
    binds = {}
    for m in lines(os.path.join(folder, "bindposes.json"), "meshes"):
        binds[m["mesh_id"]] = [column_major(x["serialized_row_major"]) for x in sorted(m.get("m_BindPose") or [], key=lambda x: x["index"])]
    meshes = {}
    stats = defaultdict(int)
    for p in lines(os.path.join(folder, "renderers_and_bones.json"), "prefabs"):
        cat = category(p)
        stats["prefabs"] += 1
        if only and cat not in only:
            continue
        for r in p.get("renderers") or []:
            if r.get("type") != "SkinnedMeshRenderer" or not r.get("m_Bones"):
                continue
            stats["renderers"] += 1
            mesh = r.get("mesh") or {}
            mid = mesh.get("mesh_id") or r.get("bindpose_key")
            if not mid:
                stats["no mesh"] += 1
                continue
            bones = [b.get("name") or "" for b in sorted(r["m_Bones"], key=lambda b: b["index"])]
            if any(not b for b in bones):
                stats["empty bone"] += 1
                continue
            parents = [int(b.get("parent_index", -1)) for b in sorted(r["m_Bones"], key=lambda b: b["index"])]
            e = meshes.setdefault((mid, tuple(bones), tuple(parents)), {"mesh": mesh.get("name", ""), "vertices": mesh.get("vertex_count", 0),
                                                                        "names": set(), "owners": set(), "kinds": set()})
            e["names"].add(r.get("name", ""))
            e["owners"].add(p.get("owner", ""))
            e["kinds"].add(cat)
    out, rigs = [], defaultdict(int)
    for (mid, _, _) in meshes:
        rigs[mid] += 1
    stats["meshes"] = len(rigs)
    stats["meshes with several bone lists"] = sum(1 for n in rigs.values() if n > 1)
    for (mid, bones, parents), e in sorted(meshes.items(), key=lambda kv: (kv[1]["mesh"], kv[0])):
        b = binds.get(mid)
        if b is None:
            stats["bone lists of a mesh with no bind poses"] += 1
            continue
        if len(b) != len(bones):
            stats["bone lists not one per bind pose"] += 1
            continue
        out.append({"mesh_id": mid, "name": sorted(e["names"])[0], "mesh": e["mesh"], "vertices": e["vertices"],
                    "kinds": sorted(e["kinds"]), "owners": sorted(e["owners"])[:16], "owner_count": len(e["owners"]),
                    "bones": list(bones), "parents": list(parents), "bindposes": [v for m in b for v in m]})
        stats["records written"] += 1
    stats["meshes written"] = len({r["mesh_id"] for r in out})
    return out, stats


def write(records, path):
    with open(path, "w", encoding="utf-8") as f:
        f.write('{"format":"gibox-offline-skeletons","version":1,"meshes":[')
        for i, r in enumerate(records):
            body = json.dumps({k: v for k, v in r.items() if k != "bindposes"}, ensure_ascii=False, separators=(",", ":"))[:-1]
            f.write(("," if i else "") + "\n" + body + ',"bindposes":[' + ",".join(number(v) for v in r["bindposes"]) + "]}")
        f.write("\n]}\n")


def check(records, skeleton_path):
    """Every renderer of a device bone library against the offline record of the same mesh (name, vertices, bones)."""
    dev = json.load(open(skeleton_path, encoding="utf-8"))
    same = differ = absent = 0
    for d in dev.get("renderers", []):
        t = d.get("target", {})
        cands = [r for r in records if r["mesh"] == t.get("mesh") and r["vertices"] == t.get("vertices") and r["bones"] == d["bones"]]
        if not cands:
            absent += 1
            print("  %s: no offline mesh %s/%s with these bones" % (d["name"], t.get("mesh"), t.get("vertices")))
            continue
        ok = any(r["parents"] == d["parents"] and r["bindposes"] == [f32(v) for v in d["bindposes"]] for r in cands)
        same += ok
        differ += not ok
        if not ok:
            print("  %s: bones found, parents or bind poses differ" % d["name"])
    print("%s: %d renderers the same, %d different, %d not found" % (skeleton_path, same, differ, absent))
    return differ == 0


def self_test():
    rm = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 1]
    assert column_major(rm) == [1, 5, 9, 0, 2, 6, 10, 0, 3, 7, 11, 0, 4, 8, 12, 1]
    assert category({"owner": "Flycloak_Wings"}) == "glider" and category({"owner": "MonsterObj_X"}) == "monster" and category({"category": "NPC"}) == "npc"
    assert number(f32(1.74979198e-06)) == "1.74979198e-06" and f32(float(number(f32(0.1)))) == f32(0.1)
    print("Offline skeletons: PASS (row-major to column-major bind poses, prefab kinds, float32 text round trip)")


def main():
    args = sys.argv[1:]
    if args == ["--self-test"]:
        self_test()
        return

    def take(flag, default=None, many=False):
        vals = []
        while flag in args:
            i = args.index(flag)
            vals.append(args[i + 1])
            del args[i:i + 2]
        return vals if many else (vals[-1] if vals else default)
    out = take("-o", "skeletons.json")
    checks = take("--check", many=True)
    only = set(take("--only", "").split(",")) - {""}
    if len(args) != 1 or not os.path.isdir(args[0]) or not only <= set(CATEGORIES):
        print(__doc__)
        sys.exit(2)
    records, stats = build(args[0], only)
    write(records, out)
    print("; ".join("%s %d" % kv for kv in sorted(stats.items())) + "; -> %s (%d bytes)" % (out, os.path.getsize(out)))
    by = defaultdict(set)
    for r in records:
        for k in r["kinds"]:
            by[k].add(r["mesh_id"])
    by = {k: len(v) for k, v in by.items()}
    print("meshes by kind: " + ", ".join("%s %d" % kv for kv in sorted(by.items())))
    ok = all([check(records, c) for c in checks])
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
