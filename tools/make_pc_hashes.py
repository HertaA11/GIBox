#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compacts the PC hash database of GI-Model-Importer-Assets into app/src/main/assets/pc-hashes.json.

The source: SilentNightSound/GI-Model-Importer-Assets (GPL-3.0), PlayerCharacterData/<name>/hash.json, the hashes the
3DMigoto PC game shows for every playable character: the index buffer of each component, the index where each of its
draws starts (object_indexes, = match_first_index) and the textures each draw binds (Diffuse / LightMap / NormalMap ...).
GIBox keeps what it compares on Android: the index-buffer hash, the starts (and counts where given) and the textures.
Each PC character is tied to the game's AvatarId through the names in Characters.java (outfits by prefix:
AyakaSpringbloom -> ayaka); a name it cannot tie keeps id 0 and is found by its index-buffer hashes instead.

    python3 tools/make_pc_hashes.py <checkout of GI-Model-Importer-Assets> [out.json]
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "app", "src", "main", "assets", "pc-hashes.json")
CHARACTERS = os.path.join(HERE, "..", "app", "src", "main", "java", "com", "gibox", "tool", "Characters.java")
HEX8 = re.compile(r"^[0-9a-f]{8}$")


def avatars():
    """AvatarId -> lower-case names (internal model name and aliases) from Characters.java."""
    out = {}
    for m in re.finditer(r'a\((\d{8}),\s*"[^"]*",\s*"([^"]*)",\s*"([^"]*)"\)', open(CHARACTERS, encoding="utf-8").read()):
        out[int(m.group(1))] = [w for w in (m.group(2) + " " + m.group(3)).lower().split() if w]
    return out


def avatar_of(folder, table):
    """The AvatarId of a PC folder: a name equal to it, else the longest name it starts with (an outfit)."""
    f = folder.lower()
    exact = [i for i, names in table.items() if f in names]
    if len(exact) == 1:
        return exact[0], "name"
    best, hit = 0, []
    for i, names in table.items():
        for n in names:
            if len(n) >= 4 and f.startswith(n):
                if len(n) > best:
                    best, hit = len(n), [i]
                elif len(n) == best and i not in hit:
                    hit.append(i)
    return (hit[0], "outfit") if len(hit) == 1 else (0, "")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else OUT
    commit = subprocess.run(["git", "-C", src, "log", "-1", "--format=%H %cs"], capture_output=True, text=True, check=True).stdout.split()
    table = avatars()
    base = os.path.join(src, "PlayerCharacterData")
    chars, untied = [], []
    for folder in sorted(os.listdir(base)):
        p = os.path.join(base, folder, "hash.json")
        if not os.path.isfile(p):
            continue
        comps = []
        for e in json.load(open(p, encoding="utf-8")):
            ib = e.get("ib", "").lower()
            idx = [int(x) for x in e.get("object_indexes", [])]
            tex = [[[t[0], t[2].lower()] for t in sub if HEX8.match(t[2].lower())] for sub in e.get("texture_hashes", [])]
            c = {"name": e.get("component_name", ""), "ib": ib if HEX8.match(ib) else ""}
            if c["ib"]:
                c["indexes"] = idx
            if e.get("object_index_counts"):
                c["counts"] = [int(x) for x in e["object_index_counts"]]
            c["textures"] = tex
            comps.append(c)
        aid, how = avatar_of(folder, table)
        if not aid:
            untied.append(folder)
        chars.append({"name": folder, "avatar": aid, "components": comps})
    doc = {"format": "gibox-pc-hashes", "version": 1,
           "source": "https://github.com/SilentNightSound/GI-Model-Importer-Assets", "commit": commit[0], "date": commit[1],
           "path": "PlayerCharacterData/*/hash.json", "license": "GPL-3.0",
           "note": "index-buffer hash, draw starts (match_first_index) and texture hashes of each component as the PC game shows them; avatar 0 = not tied to an AvatarId by name",
           "characters": chars}
    with open(out, "w", encoding="utf-8") as f:
        json.dump(doc, f, ensure_ascii=False, separators=(",", ":"))
        f.write("\n")
    n = sum(len(c["components"]) for c in chars)
    print("%d characters, %d components -> %s (%d bytes); not tied to an AvatarId: %s" % (len(chars), n, out, os.path.getsize(out), ", ".join(untied) or "none"))


if __name__ == "__main__":
    main()
