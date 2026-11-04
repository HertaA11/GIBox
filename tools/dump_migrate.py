#!/usr/bin/env python3
"""GIBox: carry the RVAs the code uses from an old dump.cs to a new one (a game update).

    python3 tools/dump_migrate.py OLD_dump.cs NEW_dump.cs [--src app/src/main/cpp] [--out migrate]
        [--class OLD.FullName ...]   # also print a class's fields old -> new (structure offsets)

Reads both dump layouts: the standard Il2CppDumper one ("// RVA: 0x.. Offset: .. VA: .. MethodIndex: N",
"// FullName: X", fields "// 0x18; FieldIndex: N") and the "MHY V26" declaration dump ("// MethodIndex: N | RVA: 0x..",
"// Image: X | TypeDefIndex: N", fields "instance_offset: 0x18").

Every RVA in the sources (offsets.hpp's enum, static_cast<abi::Offset>(0x..), the glow-trace table, *Rva = 0x..) is looked
up in the old dump, then found in the new one:
  EXACT        same class, same method name and signature (names that are not obfuscated: UnityEngine.*, System.*)
  STRUCTURAL   obfuscated names (11 capital letters, renamed each version): the class matched by its shape (image, base,
               interfaces, fields with types and offsets, method signatures with the obfuscated names blanked), the method
               by its signature in it; ORDER when several methods share that signature (taken by their order in the class)
  AMBIGUOUS    the best class is not clearly better than the next: the candidates are listed, nothing is chosen
  NOT_IN_DUMP  no method of the old dump has that RVA: a native function or data (BinDiff / disassembly, not this tool)
  NOT_FOUND    the class or method has no counterpart

Writes <out>.csv (one row per RVA) and <out>.md (a summary with the uncertain ones first). It never edits the sources:
every new RVA is a candidate to check (the first bytes, how it is called) before it ships.
"""
import argparse
import csv
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

OBF = re.compile(r"(?<![A-Za-z0-9_])[A-Z]{11}(?![A-Za-z0-9_])")
KINDS = ("class", "struct", "interface", "enum")


# ---------------------------------------------------------------- parsing helpers
def split_top(s, sep=","):
    """Split at sep outside <>, [], ()."""
    out, depth, cur = [], 0, []
    for ch in s:
        if ch in "<[(":
            depth += 1
        elif ch in ">])":
            depth -= 1
        if ch == sep and depth == 0:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if cur or out:
        out.append("".join(cur))
    return [x.strip() for x in out if x.strip()]


def tokens_top(s):
    """Whitespace tokens, keeping generic arguments (with their spaces) together."""
    out, depth, cur = [], 0, []
    for ch in s:
        if ch in "<[(":
            depth += 1
        elif ch in ">])":
            depth -= 1
        if ch.isspace() and depth == 0:
            if cur:
                out.append("".join(cur))
                cur = []
        else:
            cur.append(ch)
    if cur:
        out.append("".join(cur))
    return out


def norm_type(t):
    t = t.strip().replace("+", ".").replace("/", ".")
    t = re.sub(r"\s*,\s*", ",", t)
    t = re.sub(r"\s*<\s*", "<", t)
    t = re.sub(r"\s*>", ">", t)
    return t


def blank(t):
    return OBF.sub("§", t)


MODS = {"public", "private", "protected", "internal", "static", "virtual", "override", "abstract", "sealed", "extern",
        "unsafe", "new", "readonly", "const", "volatile", "async", "partial", "event", "fixed"}


class Method:
    __slots__ = ("cls", "name", "ret", "params", "static", "rva", "order", "line")

    def key(self, blanked):
        f = blank if blanked else (lambda x: x)
        name = self.name if not (blanked and OBF.fullmatch(self.name)) else "§"
        return (self.static, f(name), f(self.ret), tuple(f(p) for p in self.params))

    def text(self):
        return f"{self.cls.name}.{self.name}({', '.join(self.params)})"


class Field:
    __slots__ = ("type", "name", "static", "offset")


class Type:
    __slots__ = ("name", "image", "kind", "bases", "fields", "methods", "_fp")

    def __init__(self, name, image, kind, bases):
        self.name, self.image, self.kind, self.bases = name, image, kind, bases
        self.fields, self.methods, self._fp = [], [], None

    def short(self):
        n = re.sub(r"<.*$", "", self.name)
        return n.rsplit(".", 1)[-1]

    def fingerprint(self):
        if self._fp is None:
            base = blank(self.bases[0]) if self.bases else ""
            self._fp = (Counter(m.key(True) for m in self.methods),
                        Counter((f.static, blank(f.type), f.offset if not f.static else None) for f in self.fields),
                        Counter(blank(b) for b in self.bases[1:]), base)
        return self._fp


def parse_method(line, cls, rva, order):
    s = line.strip()
    s = re.sub(r"\s*\{\s*\}\s*$", "", s)
    s = re.sub(r"\s*;\s*$", "", s)
    i = s.find("(")
    j = s.rfind(")")
    if i < 0 or j < i:
        return None
    head, params = s[:i], s[i + 1:j]
    toks = [t for t in tokens_top(head) if t not in MODS or t == "static"]
    static = "static" in toks
    toks = [t for t in toks if t != "static"]
    if not toks:
        return None
    name = toks[-1]
    ret = toks[-2] if len(toks) > 1 else "void"
    if name in (cls.short(), ".ctor") or (name == cls.name):
        name = ".cctor" if static else ".ctor"
        ret = "void"
    if name == ".cctor":
        static = True
    out = []
    for p in split_top(params):
        p = re.sub(r"\s*=.*$", "", p)
        pt = tokens_top(p)
        byref = False
        while pt and pt[0] in ("ref", "out", "in", "params", "this"):
            byref = byref or pt[0] in ("ref", "out", "in")
            pt = pt[1:]
        if len(pt) > 1:
            pt = pt[:-1]  # the parameter's name
        t = norm_type(" ".join(pt))
        if byref and not t.endswith("&"):
            t += "&"
        out.append(t)
    m = Method()
    m.cls, m.name, m.ret, m.params, m.static, m.rva, m.order, m.line = cls, name, norm_type(ret), out, static, rva, order, line.strip()
    return m


def parse_decl(line):
    """'public sealed class A.B : Base, I1' -> (kind, name, [bases])."""
    s = re.sub(r"//.*$", "", line).strip()
    m = re.search(r"(?:^|\s)(class|struct|interface|enum)\s+", s)
    if not m:
        return None
    parts = split_top(s[m.end():], ":")
    if not parts:
        return None
    bases = [norm_type(b) for b in split_top(parts[1])] if len(parts) > 1 else []
    # The V26 dump writes the implicit base (": object", ": System.ValueType", ": System.Enum"); Il2CppDumper leaves it out.
    if bases and bases[0] in IMPLICIT_BASES:
        bases = bases[1:]
    return m.group(1), class_name(parts[0]), bases


IMPLICIT_BASES = {"object", "System.Object", "Object", "System.ValueType", "ValueType", "System.Enum", "Enum"}


def class_name(n):
    """A.B<T, U> / A.B`2 / A+B -> A.B (generic arity and parameters left out of class names)."""
    n = norm_type(n)
    n = re.sub(r"`\d+", "", n)
    return re.sub(r"<.*>$", "", n)


RVA_NEW = re.compile(r"^\s*// RVA: (0x[0-9A-Fa-f]+|-1)\b")
RVA_OLD = re.compile(r"^\s*// MethodIndex: \d+ \| RVA: (0x[0-9A-Fa-f]+|-1)\b")
IMG_OLD = re.compile(r"^// Image: (.+?) \| TypeDefIndex: (\d+)")
FIELD_OFF_NEW = re.compile(r";\s*// (0x[0-9A-Fa-f]+|-?\d+)?;?\s*FieldIndex")
FIELD_OFF_OLD = re.compile(r"instance_offset: (0x[0-9A-Fa-f]+)")


def parse_dump(path):
    types, by_rva = [], defaultdict(list)
    cur, pending_rva, header, in_comment = None, None, {}, False
    order = 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            st = line.strip()
            if in_comment:
                if "*/" in st:
                    in_comment = False
                continue
            if st.startswith("/*"):
                in_comment = "*/" not in st
                continue
            if cur is None or st.startswith("// Image:"):
                m = IMG_OLD.match(st)
                if m:
                    header = {"image": m.group(1)}
                    cur = None
                    continue
                if st.startswith("// Image:"):
                    header = {"image": st[len("// Image:"):].strip()}
                    cur = None
                    continue
                if st.startswith("// FullName:"):
                    header["full"] = st[len("// FullName:"):].strip()
                    continue
                if st.startswith("// Namespace:") or st.startswith("// TypeDefIndex:"):
                    continue
                if header and not st.startswith("//") and not st.startswith("[") and st and st != "{":
                    d = parse_decl(st)
                    if d:
                        kind, name, bases = d
                        if header.get("full"):
                            name = class_name(header["full"])
                        cur = Type(name, header.get("image", ""), kind, bases)
                        types.append(cur)
                        order = 0
                        header = {}
                continue
            if st == "}":
                cur = None
                continue
            m = RVA_NEW.match(st) or RVA_OLD.match(st)
            if m:
                pending_rva = None if m.group(1) == "-1" else int(m.group(1), 16)
                continue
            if not st or st.startswith("//") or st == "{":
                continue
            if "(" in st and (st.endswith("{ }") or st.endswith(";")) and "FieldIndex" not in st:
                mt = parse_method(st, cur, pending_rva, order)
                pending_rva = None
                if mt:
                    order += 1
                    cur.methods.append(mt)
                    if mt.rva:
                        by_rva[mt.rva].append(mt)
                continue
            if "FieldIndex" in st:
                decl = st.split(";", 1)[0]
                decl = re.sub(r"\s*=.*$", "", decl)
                toks = tokens_top(decl)
                static = "static" in toks or "const" in toks
                toks = [t for t in toks if t not in MODS]
                if len(toks) < 2:
                    continue
                fl = Field()
                fl.type, fl.name, fl.static = norm_type(" ".join(toks[:-1])), toks[-1], static
                off = None
                mo = FIELD_OFF_OLD.search(st)
                if mo:
                    off = int(mo.group(1), 16)
                else:
                    mn = FIELD_OFF_NEW.search(st)
                    if mn and mn.group(1):
                        off = int(mn.group(1), 0)
                fl.offset = off
                cur.fields.append(fl)
    return types, by_rva


# ---------------------------------------------------------------- matching
def similarity(a, b):
    ma, fa, ia, ba = a.fingerprint()
    mb, fb, ib, bb = b.fingerprint()
    if ba != bb:
        return 0.0

    def jac(x, y):
        if not x and not y:
            return 1.0
        inter = sum((x & y).values())
        union = sum((x | y).values())
        return inter / union if union else 1.0
    return 0.55 * jac(ma, mb) + 0.35 * jac(fa, fb) + 0.10 * jac(ia, ib)


class Index:
    def __init__(self, types):
        self.by_name = defaultdict(list)
        self.buckets = defaultdict(list)
        for t in types:
            self.by_name[t.name].append(t)
            self.buckets[(t.image, t.kind, blank(t.bases[0]) if t.bases else "")].append(t)

    def match_class(self, old):
        """(new type or None, status, score, [(score, name)...] best first)."""
        obf = bool(OBF.search(old.name))
        same = self.by_name.get(old.name, [])
        if not obf and len(same) == 1:
            return same[0], "EXACT", similarity(old, same[0]), []
        bucket = self.buckets.get((old.image, old.kind, blank(old.bases[0]) if old.bases else ""), [])
        nm, nf = len(old.methods), len(old.fields)
        scored = []
        for t in bucket:
            if abs(len(t.methods) - nm) > max(3, nm // 5) or abs(len(t.fields) - nf) > max(3, nf // 5):
                continue
            if not obf and t.name != old.name:
                continue
            if obf and blank(t.name) != blank(old.name):
                continue
            s = similarity(old, t)
            if s > 0:
                scored.append((s, t))
        scored.sort(key=lambda x: -x[0])
        top = [(round(s, 3), t.name) for s, t in scored[:3]]
        if not scored:
            return None, "NOT_FOUND", 0.0, top
        best = scored[0][0]
        second = scored[1][0] if len(scored) > 1 else 0.0
        if best < 0.6 or best - second < 0.05:
            return None, "AMBIGUOUS", best, top
        return scored[0][1], "STRUCTURAL", best, top


def match_method(old, new_cls):
    """(new method or None, how)."""
    if not OBF.fullmatch(old.name):
        same = [m for m in new_cls.methods if m.key(False) == old.key(False)]
        if len(same) == 1:
            return same[0], "name+signature"
        same = [m for m in new_cls.methods if m.name == old.name and m.key(True) == old.key(True)]
        if len(same) == 1:
            return same[0], "name+shape"
    shape = old.key(True)
    olds = [m for m in old.cls.methods if m.key(True) == shape]
    news = [m for m in new_cls.methods if m.key(True) == shape]
    if len(news) == 1 and len(olds) == 1:
        return news[0], "signature"
    if news and len(olds) == len(news):
        return news[olds.index(old)], "ORDER (%d methods share this signature)" % len(news)
    return None, "no method with this signature (%d old, %d new)" % (len(olds), len(news))


# ---------------------------------------------------------------- sources
def source_rvas(src):
    out = defaultdict(list)
    pats = [re.compile(r"\b(\w+)\s*=\s*static_cast<abi::Offset>\((0x[0-9a-fA-F]+)\)"),
            re.compile(r"\{(0x[0-9a-fA-F]+),0x[0-9a-fA-F]+,Shape::\w+,Op::\w+,Scope::\w+,\"([^\"]+)\"\}"),
            re.compile(r"\b(\w+Rva)\s*=\s*(0x[0-9a-fA-F]+)")]
    for p in sorted(Path(src).glob("*.[ch]pp")):
        text = p.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"\benum\s+Offset\s*:\s*uintptr_t\s*\{(.*?)\};", text, re.S)
        if m:
            body = re.sub(r"//[^\n]*", "", m.group(1))
            for name, v in re.findall(r"\b([A-Za-z]\w*)\s*=\s*(0x[0-9A-Fa-f]+)", body):
                out[int(v, 16)].append(f"{p.name}:{name}")
        for i, pat in enumerate(pats):
            for a, b in pat.findall(text):
                if i == 1:
                    out[int(a, 16)].append(f"{p.name}:{b}")
                else:
                    out[int(b, 16)].append(f"{p.name}:{a}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("old")
    ap.add_argument("new")
    ap.add_argument("--src", default=str(Path(__file__).resolve().parent.parent / "app/src/main/cpp"))
    ap.add_argument("--out", default="migrate")
    ap.add_argument("--class", dest="classes", action="append", default=[], help="old full class name: print its fields old -> new")
    a = ap.parse_args()

    targets = source_rvas(a.src)
    print(f"{len(targets)} RVAs in {a.src}", file=sys.stderr)
    print("reading old dump ...", file=sys.stderr)
    old_types, old_rva = parse_dump(a.old)
    print(f"  {len(old_types)} types, {len(old_rva)} RVAs", file=sys.stderr)
    print("reading new dump ...", file=sys.stderr)
    new_types, new_rva = parse_dump(a.new)
    print(f"  {len(new_types)} types, {len(new_rva)} RVAs", file=sys.stderr)
    idx = Index(new_types)
    cls_cache = {}

    rows = []
    for rva in sorted(targets):
        names = "; ".join(targets[rva])
        olds = old_rva.get(rva, [])
        if not olds:
            rows.append(dict(name=names, old_rva=f"0x{rva:X}", new_rva="", status="NOT_IN_DUMP", old_method="", new_method="",
                             how="", score="", candidates="", shared_rva=""))
            continue
        results = []
        for om in olds:
            if id(om.cls) not in cls_cache:
                cls_cache[id(om.cls)] = idx.match_class(om.cls)
            nc, cstat, score, top = cls_cache[id(om.cls)]
            if nc is None:
                results.append((None, cstat, om, "", score, top))
                continue
            nm, how = match_method(om, nc)
            if nm is None:
                results.append((None, "NOT_FOUND", om, how, score, top))
            else:
                st = cstat if not how.startswith("ORDER") else "ORDER"
                results.append((nm, st, om, how, score, top))
        found = [r for r in results if r[0] is not None and r[0].rva]
        new_set = sorted({r[0].rva for r in found})
        pick = None
        rank = {"EXACT": 0, "STRUCTURAL": 1, "ORDER": 2}
        if found:
            pick = sorted(found, key=lambda r: (rank.get(r[1], 9), -r[4]))[0]
        r0 = pick or results[0]
        status = r0[1]
        if len(new_set) > 1:
            status = "CONFLICT"
        rows.append(dict(
            name=names, old_rva=f"0x{rva:X}", new_rva=f"0x{pick[0].rva:X}" if pick else "", status=status,
            old_method=r0[2].text(), new_method=pick[0].text() if pick else "", how=r0[3],
            score=f"{r0[4]:.3f}" if r0[4] else "",
            candidates=" | ".join(f"{n} ({s})" for s, n in r0[5]),
            shared_rva=(f"{len(olds)} old methods share this RVA; new: " + ", ".join(f"0x{x:X}" for x in new_set)) if len(olds) > 1 else ""))

    with open(a.out + ".csv", "w", encoding="utf-8-sig", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    order = ["CONFLICT", "AMBIGUOUS", "NOT_FOUND", "ORDER", "NOT_IN_DUMP", "STRUCTURAL", "EXACT"]
    rows.sort(key=lambda r: (order.index(r["status"]) if r["status"] in order else 0, r["name"]))
    with open(a.out + ".md", "w", encoding="utf-8") as f:
        f.write("# RVA migration (candidates: check each before shipping)\n\n")
        for k, v in sorted(Counter(r["status"] for r in rows).items(), key=lambda x: order.index(x[0]) if x[0] in order else 0):
            f.write(f"- {k}: {v}\n")
        f.write("\n| status | name | old | new | old method | new method | how | score | candidates |\n|---|---|---|---|---|---|---|---|---|\n")
        for r in rows:
            f.write("| " + " | ".join(str(r[k]).replace("|", "\\|") for k in
                                       ("status", "name", "old_rva", "new_rva", "old_method", "new_method", "how", "score", "candidates")) + " |\n")
        for cname in a.classes:
            olds = [t for t in old_types if t.name == cname]
            f.write(f"\n## Fields of {cname}\n\n")
            if not olds:
                f.write("not in the old dump\n")
                continue
            nc, st, score, top = idx.match_class(olds[0])
            f.write(f"new class: {nc.name if nc else '-'} ({st}, score {score:.3f}; candidates {top})\n\n")
            f.write("| old offset | old field | new offset | new field |\n|---|---|---|---|\n")
            of = [x for x in olds[0].fields if not x.static]
            nf = [x for x in nc.fields if not x.static] if nc else []
            for i, x in enumerate(of):
                y = nf[i] if i < len(nf) and blank(nf[i].type) == blank(x.type) else None
                f.write(f"| {hex(x.offset) if x.offset is not None else '?'} | {x.type} {x.name} | "
                        f"{hex(y.offset) if y and y.offset is not None else '?'} | {(y.type + ' ' + y.name) if y else '(type differs or missing)'} |\n")
    c = Counter(r["status"] for r in rows)
    print(f"wrote {a.out}.csv and {a.out}.md: " + ", ".join(f"{k} {v}" for k, v in sorted(c.items())))


if __name__ == "__main__":
    main()
