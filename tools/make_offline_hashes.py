#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds a GIBox hash map (hash-map.json format) from meshes exported from the game's files, without running the game.

The game uploads a mesh's index buffer to the GPU exactly as it is stored in its Unity Mesh object (m_IndexBuffer):
the hashes of CitlaliCostumeXia's Body (f117984b) and Bang (d44b2c85), computed here from exported index data, equal the
ones a device capture tied to those meshes, all three upload forms. So every exported mesh gives its hashes directly,
by name, with no counting of indices and vertices and no ambiguity, for meshes the game has never shown on this device.

Each mesh is a pair in one folder, as the extraction scripts write them:
  <name>.json          {"name", "vertex_count", "index_format" (0: 16-bit, 1: 32-bit), "index_bytes",
                        "submeshes": [{"first_byte", "index_count"}, ...], optional "path_evidence": [...], "owner"}
  <name>.indices.bin   the raw index buffer (index_bytes bytes)
Extracting them (decrypting the game's files) is not part of GIBox.

The hash is 3DMigoto's (vk_capture.hpp): CRC-32C of the data, then of a D3D11_BUFFER_DESC with ByteWidth = the data's
size, in the three forms PC mods were made with (DEFAULT + IB, DEFAULT + IB|SRV|UAV, IMMUTABLE + IB). A hash two
different meshes give (the same index data, e.g. a character's Pupil and an effect's sphere) goes to the one most
likely to be modded: a character's (avatar), an NPC's or monster's (owner), a weapon's (Equip_ / MonEquip_), then the
rest; two of the same kind leave it out. Only those kinds are written unless --keep all (scenery, effects and props
are most of the game's meshes and almost never modded). The result is checked against a table captured on a device
(the bundled device-hashes.json by default): a hash both have must name a mesh with the same vertex count; another
mesh name there is index data two meshes share, reported, not an error.

    python3 tools/make_offline_hashes.py --build <game Build ID> export-folder [more ...] [-o hash-map.json]
        [--keep characters|all] [--check other-map.json] [--pc app/src/main/assets/pc-hashes.json]
    python3 tools/make_offline_hashes.py --from hash-map.json [-o out.json] [--keep characters|all] [--check ...]
        (a map this tool wrote: filtered and checked again, no hashing)
    python3 tools/make_offline_hashes.py --self-test
"""
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "app", "src", "main", "assets")
KINDS = ((0, 0x2, "as uploaded, desc DEFAULT, Bind=IB"), (0, 0x8A, "as uploaded, desc DEFAULT, Bind=IB|SRV|UAV"),
         (1, 0x2, "as uploaded, desc IMMUTABLE, Bind=IB"))


def _table():
    t = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0x82F63B78 if c & 1 else c >> 1
        t.append(c)
    return t


T0 = _table()
TS = [T0]
for _k in range(1, 8):
    TS.append([(TS[_k - 1][i] >> 8) ^ T0[TS[_k - 1][i] & 0xFF] for i in range(256)])

try:  # the "crc32c" package (C) when installed; else slicing-by-8 in Python
    import crc32c as _fast

    def crc32c(seed, data):
        return _fast.crc32c(bytes(data), seed)
except ImportError:
    def crc32c(seed, data):
        c = ~seed & 0xFFFFFFFF
        n, i = len(data), 0
        t0, t1, t2, t3, t4, t5, t6, t7 = TS
        end8 = n - (n % 8)
        for (lo, hi) in struct.iter_unpack("<II", data[:end8]):
            lo ^= c
            c = (t7[lo & 0xFF] ^ t6[(lo >> 8) & 0xFF] ^ t5[(lo >> 16) & 0xFF] ^ t4[lo >> 24]
                 ^ t3[hi & 0xFF] ^ t2[(hi >> 8) & 0xFF] ^ t1[(hi >> 16) & 0xFF] ^ t0[hi >> 24])
        for b in data[end8:]:
            c = T0[(c ^ b) & 0xFF] ^ (c >> 8)
        return ~c & 0xFFFFFFFF


def hashes(data):
    """The three upload forms' hashes of one index buffer, as vk_capture.hpp computes them."""
    d = crc32c(0, data)
    return [("%08x" % crc32c(d, struct.pack("<6I", len(data), usage, bind, 0, 0, 0)), name) for usage, bind, name in KINDS]


def normalize(s):
    """core.hpp normalize: lower case, a trailing _lod<n> dropped."""
    s = s.lower()
    m = re.search(r"_lod[0-9]+$", s)
    return s[:m.start()] if m else s


def avatar_of(meta):
    for p in meta.get("path_evidence") or []:
        m = re.match(r"(avatar_[a-z0-9_]+?)(_modelavatar|_model)?$", str(p).lower())
        if m:
            return m.group(1)
    return ""


def exported(folders):
    """Every (json, indices.bin) pair under the folders."""
    for top in folders:
        for dirpath, _, files in os.walk(top):
            for f in sorted(files):
                if f.endswith(".json") and f[:-5] + ".indices.bin" in files:
                    yield os.path.join(dirpath, f), os.path.join(dirpath, f[:-5] + ".indices.bin")


def entry(meta_path, data_path, build, pc):
    meta = json.load(open(meta_path, encoding="utf-8"))
    data = open(data_path, "rb").read()
    name, verts, fmt = meta.get("name"), meta.get("vertex_count"), meta.get("index_format")
    if not isinstance(name, str) or not name or not isinstance(verts, int) or verts <= 0 or fmt not in (0, 1):
        raise ValueError("name, vertex_count or index_format missing")
    width = 2 if fmt == 0 else 4
    if meta.get("index_bytes", len(data)) != len(data) or not data or len(data) % width:
        raise ValueError("index data is %d bytes, the mesh says %s" % (len(data), meta.get("index_bytes")))
    subs = []
    for s in meta.get("submeshes") or []:
        fb, cnt = s.get("first_byte"), s.get("index_count")
        if not isinstance(fb, int) or not isinstance(cnt, int) or fb % width or fb // width + cnt > len(data) // width:
            raise ValueError("submesh %r outside the index data" % s)
        subs.append([fb // width, cnt])
    if not subs or len(subs) > 64:
        raise ValueError("%d submeshes" % len(subs))
    avatar = avatar_of(meta)
    owner = "" if avatar else str(meta.get("owner") or "")
    out = []
    for ib, variant in hashes(data):
        character, component = pc.get(ib, ("", ""))
        e = {"ib": ib, "variant": variant, "character": character, "component": component, "renderer": normalize(name),
             "mesh": name, "avatar": avatar, "build": build, "vertices": verts, "bones": 0, "submeshes": subs}
        if owner:
            e["owner"] = owner
        e["source"] = "offline"
        out.append(e)
    return out


def pc_table(path):
    pc = {}
    if path and os.path.isfile(path):
        for c in json.load(open(path, encoding="utf-8")).get("characters", []):
            for comp in c.get("components", []):
                if comp.get("ib"):
                    pc.setdefault(comp["ib"].lower(), (c.get("name", ""), comp.get("name", "")))
    return pc


def same_mesh(a, b):
    return a["mesh"] == b["mesh"] and a["vertices"] == b["vertices"] and a["submeshes"] == b["submeshes"]


def rank(e):
    """0 a character's mesh, 1 an NPC's or monster's, 2 a weapon's, 3 anything else."""
    if e.get("avatar") or e.get("shared"):
        return 0
    if e.get("owner"):
        return 1
    return 2 if re.match(r"(Equip|MonEquip)_", e["mesh"]) else 3


def choose(candidates):
    """One entry per hash: the best-ranked mesh, or None when two different meshes share the best rank. The same mesh
    under two avatars or owners (a character's original look and a skin with the same Body, Xingqiu's 76df4025) names
    neither: the App would otherwise take the hash for one outfit's and convert against the wrong skeleton."""
    best = min(rank(e) for e in candidates)
    top = [e for e in candidates if rank(e) == best]
    first = top[0]
    if not all(same_mesh(first, e) for e in top[1:]):
        return None
    if len({e.get("avatar", "") for e in top}) > 1 or len({e.get("owner", "") for e in top}) > 1:
        first = dict(first, avatar="")
        first.pop("owner", None)
        first["shared"] = sorted({e.get("avatar") or e.get("owner") or "" for e in top} - {""})[:8]
    return first


def self_test():
    assert hashes(struct.pack("<6H", 0, 1, 2, 2, 1, 3)) == [("c33fba30", KINDS[0][2]), ("fbebd0ef", KINDS[1][2]), ("f47c4d8d", KINDS[2][2])]
    assert hashes(struct.pack("<3I", 0, 1, 2))[1][0] == "7da3cfcd"
    big = bytes(range(256)) * 41  # every byte value, a length that is no multiple of 8
    slow = ~0 & 0xFFFFFFFF
    for b in big:
        slow = T0[(slow ^ b) & 0xFF] ^ (slow >> 8)
    assert crc32c(0, big) == ~slow & 0xFFFFFFFF
    assert avatar_of({"path_evidence": ["Avatar_Girl_Catalyst_CitlaliCostumeXia_ModelAvatar"]}) == "avatar_girl_catalyst_citlalicostumexia"
    assert normalize("Body_LOD1") == "body" and normalize("Face_Eye") == "face_eye"
    pupil = {"mesh": "Pupil", "vertices": 120, "submeshes": [[0, 300]], "avatar": "avatar_girl_sword_x"}
    sphere = {"mesh": "Eff_Sphere", "vertices": 120, "submeshes": [[0, 300]], "avatar": ""}
    npc = {"mesh": "Eye", "vertices": 120, "submeshes": [[0, 300]], "avatar": "", "owner": "NPC_Boy"}
    assert choose([sphere, pupil]) is pupil and choose([sphere, npc]) is npc and choose([pupil, dict(pupil, mesh="Eye")]) is None
    assert rank({"mesh": "Equip_Sword_Foo", "avatar": ""}) == 2 and rank(sphere) == 3
    skin = dict(pupil, avatar="avatar_girl_sword_xcostume")
    both = choose([pupil, skin])
    assert both["avatar"] == "" and both["shared"] == ["avatar_girl_sword_x", "avatar_girl_sword_xcostume"] and pupil["avatar"]
    print("Offline hashes: PASS (3DMigoto CRC-32C and buffer desc forms, 16/32-bit index data, avatar from the path, renderer name)")


def main():
    args = sys.argv[1:]
    if args == ["--self-test"]:
        self_test()
        return

    def take(flag, default=None):
        if flag in args:
            i = args.index(flag)
            v = args[i + 1]
            del args[i:i + 2]
            return v
        return default
    out = take("-o", "hash-map.json")
    build = take("--build")
    source = take("--from")
    keep = take("--keep", "characters")
    check = take("--check", os.path.join(ASSETS, "device-hashes.json"))
    pc = pc_table(take("--pc", os.path.join(ASSETS, "pc-hashes.json")))
    if keep not in ("characters", "all") or (not source and (not args or not build or not re.fullmatch(r"[0-9a-f]{40}", build))):
        print(__doc__)
        sys.exit(2)
    candidates, failed, meshes = {}, 0, 0
    if source:
        for e in json.load(open(source, encoding="utf-8"))["meshes"]:
            candidates.setdefault(e["ib"], []).append(e)
            meshes += 1
    for meta, data in exported(args):
        try:
            entries = entry(meta, data, build, pc)
        except (ValueError, OSError, json.JSONDecodeError) as ex:
            failed += 1
            print("skipped %s: %s" % (meta, ex))
            continue
        meshes += 1
        for e in entries:
            candidates.setdefault(e["ib"], []).append(e)
    by_ib, shared, ambiguous, dropped = {}, 0, 0, 0
    for ib, cs in candidates.items():
        e = choose(cs)
        if e is None:
            ambiguous += 1
            continue
        shared += any(not same_mesh(e, x) for x in cs)
        if keep == "all" or rank(e) < 3:
            by_ib[ib] = e
        else:
            dropped += 1
    # Against a device capture: a hash both have names a mesh with the same vertex count (the same index data).
    agree = other = differ = 0
    if check and os.path.isfile(check):
        for d in json.load(open(check, encoding="utf-8")).get("meshes", []):
            e = by_ib.get(d["ib"])
            if e is None:
                continue
            if same_mesh(d, e):
                agree += 1
            elif d["vertices"] == e["vertices"]:
                other += 1
                print("shared index data: %s is %s there, %s (%s) here" % (d["ib"], d["mesh"], e["mesh"], e.get("avatar") or e.get("owner") or "-"))
            else:
                differ += 1
                print("DIFFERS from %s: %s is %s/%d here, %s/%d there" % (check, d["ib"], e["mesh"], e["vertices"], d["mesh"], d["vertices"]))
    body = '{"format":"gibox-hash-map","version":1,"meshes":[' + ",".join(
        "\n" + json.dumps(by_ib[k], ensure_ascii=False, separators=(",", ":")) for k in sorted(by_ib)) + "\n]}\n"
    with open(out, "w", encoding="utf-8") as f:
        f.write(body)
    kinds = [0, 0, 0, 0]
    for e in by_ib.values():
        kinds[rank(e)] += 1
    print("%d meshes read (%d skipped); %d hashes written to %s (characters %d, NPCs / monsters %d, weapons %d, other %d); "
          "%d other (--keep all to write them), %d shared by meshes of different kinds (the likelier kept), %d left out (two meshes of one kind); "
          "against %s: %d the same, %d shared index data, %d different"
          % (meshes, failed, len(by_ib), out, kinds[0], kinds[1], kinds[2], kinds[3], dropped, shared, ambiguous, check, agree, other, differ))
    if differ:
        sys.exit(1)


if __name__ == "__main__":
    main()
