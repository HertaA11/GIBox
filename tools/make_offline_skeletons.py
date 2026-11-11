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

Each prefab is listed after the meshes with the record of each of its renderers, so that the App finds a character's
or an outfit's renderers by the prefab name (Avatar_Girl_Catalyst_Citlali). The submesh count a bone library's target
carries is the mesh's own when bindposes.json has it (submesh_count, or the m_SubMeshes list as submeshes), else
taken from hash tables (--hashes), where one count is known for the mesh's name and vertex count.

    python3 tools/make_offline_skeletons.py export-folder [-o skeletons.json] [--check skeleton.json ...]
        [--only npc,monster,glider,character] [--hashes device-hashes.json ...] [--build <game Build ID>]
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


def submesh_counts(paths):
    """(mesh name, vertex count) -> its submesh count, where the hash tables give one."""
    seen = defaultdict(set)
    for path in paths:
        for e in json.load(open(path, encoding="utf-8")).get("meshes", []):
            seen[(e.get("mesh"), e.get("vertices"))].add(len(e.get("submeshes") or []))
    return {k: next(iter(v)) for k, v in seen.items() if len(v) == 1}


def build(folder, only, counts=None):
    binds, own_counts = {}, {}
    for m in lines(os.path.join(folder, "bindposes.json"), "meshes"):
        binds[m["mesh_id"]] = [column_major(x["serialized_row_major"]) for x in sorted(m.get("m_BindPose") or [], key=lambda x: x["index"])]
        # The mesh's own submesh count when the export carries it (m_SubMeshes or a count); else the hash tables'.
        mm = m.get("mesh") or {}
        n = m.get("submesh_count", mm.get("submesh_count"))
        subs = m.get("submeshes", mm.get("submeshes"))
        if isinstance(n, int) and n > 0:
            own_counts[m["mesh_id"]] = n
        elif isinstance(subs, list) and subs:
            own_counts[m["mesh_id"]] = len(subs)
    meshes, uses = {}, {}
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
            uses.setdefault(p.get("owner", ""), {"kind": cat, "renderers": {}})["renderers"].setdefault(r.get("name", ""), set()).add((mid, tuple(bones), tuple(parents)))
    out, rigs, index = [], defaultdict(int), {}
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
        rec = {"mesh_id": mid, "name": sorted(e["names"])[0], "mesh": e["mesh"], "vertices": e["vertices"]}
        sub = own_counts.get(mid) or (counts or {}).get((e["mesh"], e["vertices"]))
        if sub:
            rec["submeshes"] = sub
        rec.update({"kinds": sorted(e["kinds"]), "owner_count": len(e["owners"]), "bones": list(bones), "parents": list(parents),
                    "bindposes": [v for m in b for v in m]})
        index[(mid, bones, parents)] = len(out)
        out.append(rec)
        stats["records written"] += 1
    stats["meshes written"] = len({r["mesh_id"] for r in out})
    stats["records with a submesh count"] = sum(1 for r in out if "submeshes" in r)
    # Per prefab: renderer name -> record; a renderer name two records answer for one prefab (two instances that differ) is left out.
    prefabs = []
    for owner, u in sorted(uses.items()):
        rs = []
        for name, keys in sorted(u["renderers"].items()):
            got = sorted({index[k] for k in keys if k in index})
            # Two copies of one prefab (two bundles) give the same renderer twice: one record when they hold the same.
            same = lambda a, b: all(out[a].get(f) == out[b].get(f) for f in ("mesh", "vertices", "bones", "parents", "bindposes"))
            if got and len(got) == len(keys) and all(same(got[0], g) for g in got[1:]):
                rs.append([name, got[0]])
            else:
                stats["prefab renderers left out"] += 1
        if rs:
            prefabs.append({"owner": owner, "kind": u["kind"], "renderers": rs})
    stats["prefabs written"] = len(prefabs)
    return out, prefabs, stats


def write(records, prefabs, path, build=""):
    """One record per line: the header line, the meshes, a line opening the prefabs, the prefabs, the closing line."""
    with open(path, "w", encoding="utf-8") as f:
        f.write('{"format":"gibox-offline-skeletons","version":1,"build":%s,"meshes":[' % json.dumps(build))
        for i, r in enumerate(records):
            body = json.dumps({k: v for k, v in r.items() if k != "bindposes"}, ensure_ascii=False, separators=(",", ":"))[:-1]
            f.write(("," if i else "") + "\n" + body + ',"bindposes":[' + ",".join(number(v) for v in r["bindposes"]) + "]}")
        f.write('\n],"prefabs":[')
        for i, p in enumerate(prefabs):
            f.write(("," if i else "") + "\n" + json.dumps(p, ensure_ascii=False, separators=(",", ":")))
        f.write("\n]}\n")


def prefab_of(root):
    """The prefab a device bone library's root path names: the segment the game instantiated, "(Clone)" dropped."""
    for seg in root.split("/"):
        if seg.endswith("(Clone)"):
            return seg[:-len("(Clone)")]
    return ""


def check(records, prefabs, skeleton_path):
    """A device bone library against the offline table as the App reads it: the prefab its root names, each renderer's
    record by name, and that record's bones, parents, bind poses and submesh count."""
    dev = json.load(open(skeleton_path, encoding="utf-8"))
    owner = prefab_of(dev.get("root", ""))
    p = next((x for x in prefabs if x["owner"] == owner), None)
    by_name = dict((n, records[i]) for n, i in p["renderers"]) if p else {}
    same = differ = absent = 0
    for d in dev.get("renderers", []):
        t = d.get("target", {})
        r = by_name.get(d["name"])
        if r is None:
            absent += 1
            print("  %s: %s has no offline renderer %s" % (skeleton_path, owner or "?", d["name"]))
            continue
        ok = (r["mesh"] == t.get("mesh") and r["vertices"] == t.get("vertices") and r["bones"] == d["bones"] and r["parents"] == d["parents"]
              and r["bindposes"] == [f32(v) for v in d["bindposes"]] and r.get("submeshes", t.get("submeshes")) == t.get("submeshes"))
        same += ok
        differ += not ok
        if not ok:
            print("  %s: %s %s differs" % (skeleton_path, owner, d["name"]))
    print("%s (%s): %d renderers the same, %d different, %d not found" % (skeleton_path, owner or "?", same, differ, absent))
    return differ == 0


def self_test():
    rm = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 1]
    assert column_major(rm) == [1, 5, 9, 0, 2, 6, 10, 0, 3, 7, 11, 0, 4, 8, 12, 1]
    assert category({"owner": "Flycloak_Wings"}) == "glider" and category({"owner": "MonsterObj_X"}) == "monster" and category({"category": "NPC"}) == "npc"
    assert number(f32(1.74979198e-06)) == "1.74979198e-06" and f32(float(number(f32(0.1)))) == f32(0.1)
    assert prefab_of("EntityRoot/AvatarRoot/Avatar_Girl_Catalyst_Citlali(Clone)/OffsetDummy/Avatar_Girl_Catalyst_Citlali/Bip001") == "Avatar_Girl_Catalyst_Citlali"
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
    hashes = take("--hashes", many=True)
    build_id = take("--build", "")
    if len(args) != 1 or not os.path.isdir(args[0]) or not only <= set(CATEGORIES):
        print(__doc__)
        sys.exit(2)
    records, prefabs, stats = build(args[0], only, submesh_counts(hashes))
    write(records, prefabs, out, build_id)
    print("; ".join("%s %d" % kv for kv in sorted(stats.items())) + "; -> %s (%d bytes)" % (out, os.path.getsize(out)))
    by = defaultdict(set)
    for r in records:
        for k in r["kinds"]:
            by[k].add(r["mesh_id"])
    by = {k: len(v) for k, v in by.items()}
    print("meshes by kind: " + ", ".join("%s %d" % kv for kv in sorted(by.items())))
    ok = all([check(records, prefabs, c) for c in checks])
    if not ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
