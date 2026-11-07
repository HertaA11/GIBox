#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds app/src/main/assets/outfits.json: GIBox's own table of each character's outfits (original look and skins).

A skin is its own model in the game (its own prefab, mesh and bone list), so a mod made for one outfit must be
converted against that outfit's bone library and shown only on it. The table ties together what names an outfit:

  skin   the game's skin ID (AvatarCostumeExcelConfigData skinId; the original look has its own, isDefault)
  prefab the model's root name as the runtime reads it from the renderer (lower case, "_manekin" dropped): a skin's
         jsonName; the original look is that name without "Costume..." (every skin prefab is <original>Costume<X>)
  name / en  the skin's name in Chinese / English (TextMap_Medium*), "原皮" / "Original" for the original look
  pc     the GI-Model-Importer-Assets characters (pc-hashes.json) that are this outfit: by the device hash map (an
         index-buffer hash a capture tied to this prefab), then by elimination (one outfit and one PC entry left),
         then MANUAL (where the PC data is wrong or names do not line up)

Only characters with at least one skin are listed; every other character has one outfit, its original look.

    python3 tools/make_outfits.py <YuanShenResources checkout or folder> [-o out.json]

The folder must hold ExcelBinOutput/AvatarCostumeExcelConfigData.json and TextMap/TextMap_MediumCHS.json,
TextMap_MediumEN.json (or the three files side by side).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "app", "src", "main", "assets")
OUT = os.path.join(ASSETS, "outfits.json")
SOURCE = "https://gitlab.com/GuraFoundation/YuanShenResources"
COMMIT = "f11cf545a7eee32a29999f960ebfba305f896ed3"   # CNCBWin7.2.50 (2026-11-04)
# PC entries no capture has placed yet, by skin ID (0: the original look); checked by name against the skin names.
# JeanCN and XingqiuBamboo: GI-Model-Importer-Assets gives them the original's index buffer (Jean 115737ff, Xingqiu
# 76df4025); on the device Gunnhildr's Legacy (QinCostumeWic) is 920c0b3f, so their PC hash names the original look.
MANUAL = {"JeanCN": 200302, "XingqiuBamboo": 202501, "BarbaraSummertime": 201401, "FischlHighness": 203101,
          "GanyuTwilight": 203701, "KaeyaSailwind": 201501, "MonaCN": 204101, "NingguangOrchid": 202701,
          "Barbara": 0, "Bennett": 0, "Charlotte": 0, "Citlali": 0, "Fischl": 0, "Ganyu": 0, "HuTao": 0, "Kaeya": 0,
          "Kirara": 0, "Mona": 0, "Neuvillette": 0, "Ningguang": 0, "TravelerBoy": 0, "Xiangling": 0, "Xingqiu": 0,
          "Yelan": 0}


def find(root, *names):
    for n in names:
        for p in (os.path.join(root, n), os.path.join(root, os.path.basename(n))):
            if os.path.isfile(p):
                return p
    raise SystemExit("missing %s under %s" % (names[0], root))


def base(prefab):
    i = prefab.find("costume")
    return prefab[:i] if i > 0 else prefab


def main():
    args, out = sys.argv[1:], OUT
    if "-o" in args:
        i = args.index("-o")
        out = args[i + 1]
        args = args[:i] + args[i + 2:]
    if len(args) != 1:
        print(__doc__)
        sys.exit(2)
    root = args[0]
    costumes = json.load(open(find(root, "ExcelBinOutput/AvatarCostumeExcelConfigData.json"), encoding="utf-8"))
    zh = json.load(open(find(root, "TextMap/TextMap_MediumCHS.json"), encoding="utf-8"))
    en = json.load(open(find(root, "TextMap/TextMap_MediumEN.json"), encoding="utf-8"))
    pc = json.load(open(os.path.join(ASSETS, "pc-hashes.json"), encoding="utf-8"))
    device = json.load(open(os.path.join(ASSETS, "device-hashes.json"), encoding="utf-8"))

    chars = {}
    for c in costumes:
        cid = c["characterId"]
        x = chars.setdefault(cid, {"id": cid, "default": None, "skins": []})
        if c.get("isDefault"):
            x["default"] = c["skinId"]
        elif c.get("jsonName"):
            h = str(c.get("nameTextMapHash"))
            name, eng = zh.get(h), en.get(h)
            if not name or not eng:
                raise SystemExit("no name for skin %s" % c["skinId"])
            x["skins"].append({"skin": c["skinId"], "prefab": c["jsonName"].lower(), "name": name, "en": eng, "pc": []})
    chars = {k: v for k, v in chars.items() if v["skins"]}

    byAvatar = {}
    for m in device["meshes"]:
        if m.get("avatar"):
            byAvatar.setdefault(m["ib"], m["avatar"])
    outfits = {}
    for cid, x in chars.items():
        prefabs = {base(s["prefab"]) for s in x["skins"]}
        if len(prefabs) != 1:
            raise SystemExit("skins of %d have different originals: %s" % (cid, prefabs))
        if not x["default"]:
            raise SystemExit("no default skin ID for %d" % cid)
        original = {"skin": x["default"], "prefab": prefabs.pop(), "name": "原皮", "en": "Original", "pc": []}
        outfits[cid] = [original] + sorted(x["skins"], key=lambda s: s["skin"])

    notes = []
    for c in pc["characters"]:
        cid = c.get("avatar")
        if cid not in outfits:
            continue
        mine = {o["prefab"]: o for o in outfits[cid]}
        prefab, how = None, "manual"
        if c["name"] in MANUAL:
            want = MANUAL[c["name"]]
            prefab = next((o["prefab"] for o in outfits[cid] if (o["skin"] == want if want else o["name"] == "原皮")), None)
        else:
            seen = {byAvatar[p["ib"]] for p in c["components"] if p.get("ib") in byAvatar}
            seen &= set(mine)
            if len(seen) == 1:
                prefab, how = seen.pop(), "device hash map"
        if prefab:
            if prefab not in mine:
                raise SystemExit("%s: %s is not an outfit of %d" % (c["name"], prefab, cid))
            mine[prefab]["pc"].append(c["name"])
            notes.append("%s -> %s (%s)" % (c["name"], prefab, how))
        else:
            notes.append("%s: left for elimination" % c["name"])
    for c in pc["characters"]:   # one outfit without a PC entry and one PC entry without an outfit: they are the pair
        cid = c.get("avatar")
        if cid not in outfits or any(c["name"] in o["pc"] for o in outfits[cid]):
            continue
        free = [o for o in outfits[cid] if not o["pc"]]
        left = [p["name"] for p in pc["characters"] if p.get("avatar") == cid and not any(p["name"] in o["pc"] for o in outfits[cid])]
        if len(free) == 1 and left == [c["name"]]:
            free[0]["pc"].append(c["name"])
            notes.append("%s -> %s (elimination)" % (c["name"], free[0]["prefab"]))
        else:
            notes.append("%s: not placed (%d outfits and %d PC entries without a match)" % (c["name"], len(free), len(left)))

    table = {"format": "gibox-outfits", "version": 1, "source": SOURCE, "commit": COMMIT,
             "pc": pc.get("source", "") + " @" + pc.get("commit", ""),
             "characters": [{"id": cid, "outfits": outfits[cid]} for cid in sorted(outfits)]}
    body = json.dumps(table, ensure_ascii=False, indent=1)
    with open(out, "w", encoding="utf-8") as f:
        f.write(body + "\n")
    for n in notes:
        print(n)
    print("%d characters, %d skins -> %s" % (len(outfits), sum(len(v) - 1 for v in outfits.values()), out))


if __name__ == "__main__":
    main()
