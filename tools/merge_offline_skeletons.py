#!/usr/bin/env python3
"""Merge character-page skeleton exports into the bundled table, remapping mesh indices.

python3 tools/merge_offline_skeletons.py app/src/main/assets/offline-skeletons.json contribution.json -o merged.json

Requires matching, known game Build IDs. Old captures without a Build ID require
--build <verified Build ID>. Conflicting records are rejected, never silently overwritten.
"""
import argparse
import json
import math
import os
import struct
import tempfile


def signature(m):
    names, parents, matrices = m['bones'], m['parents'], m['bindposes']
    n = len(names)
    if not (0 < n <= 512 and len(parents) == n and len(matrices) == n * 16):
        raise ValueError('骨骼或矩阵数量无效')
    if not all(isinstance(b, str) and b and len(b) <= 256 and not any(c in b for c in '\t\r\n') for b in names):
        raise ValueError('骨骼名称无效')
    for i in range(n):
        seen = set()
        while i != -1:
            if type(i) is not int or not 0 <= i < n or i in seen:
                raise ValueError('父骨骼编号无效或形成循环')
            seen.add(i)
            i = parents[i]
    if not all(isinstance(v, (int, float)) and math.isfinite(v) for v in matrices):
        raise ValueError('矩阵含非有限数值')
    values = struct.pack('<%df' % len(matrices), *matrices)
    if not all(math.isfinite(v) for v in struct.unpack('<%df' % len(matrices), values)):
        raise ValueError('矩阵超出 float32 范围')
    floats = struct.unpack('<%df' % len(matrices), values)
    for offset in range(0, len(floats), 16):
        a = floats[offset:offset+16]
        det = (a[0]*(a[5]*a[10]-a[9]*a[6]) - a[4]*(a[1]*a[10]-a[9]*a[2])
               + a[8]*(a[1]*a[6]-a[5]*a[2]))
        if abs(det) <= 1e-12 or abs(a[15]) <= 1e-12:
            raise ValueError('绑定矩阵不可逆')
    if not isinstance(m.get('mesh'), str) or not m['mesh'] or not 0 < m['vertices'] <= 500000:
        raise ValueError('网格信息无效')
    return m['mesh'], m['vertices'], tuple(names), tuple(parents), values


def merge(paths, out, verified_build=None):
    records, prefabs, by_shape, build = [], {}, {}, verified_build or ''
    for path in paths:
        with open(path, encoding='utf-8-sig') as f:
            doc = json.load(f)
        if doc.get('format') != 'gibox-offline-skeletons' or doc.get('version') != 1:
            raise ValueError('%s：不是内置骨骼表格式' % path)
        b = doc.get('build') or verified_build
        if not b:
            raise ValueError('%s：没有游戏 Build ID，核实版本后用 --build 指定' % path)
        if build and build != b:
            raise ValueError('%s：游戏版本不同' % path)
        build = b
        local = []
        for m in doc['meshes']:
            key = signature(m)
            if key not in by_shape:
                by_shape[key] = len(records)
                records.append(dict(m))
            idx = by_shape[key]
            sub = m.get('submeshes')
            old = records[idx].get('submeshes')
            if sub is not None:
                if type(sub) is not int or not 1 <= sub <= 64 or old is not None and old != sub:
                    raise ValueError('%s：子网格数量无效或冲突' % path)
                records[idx]['submeshes'] = sub
            local.append(idx)
        for p in doc['prefabs']:
            owner = p['owner']
            if not isinstance(owner, str) or not owner or len(owner) > 256:
                raise ValueError('预制体名称无效')
            key = owner.lower()
            if key not in prefabs:
                prefabs[key] = dict(p, renderers=[])
            dest = prefabs[key]
            rs = {name: idx for name, idx in dest['renderers']}
            for name, idx in p['renderers']:
                if type(idx) is not int or not 0 <= idx < len(local):
                    raise ValueError('网格引用越界：' + owner)
                ref = local[idx]
                if name in rs and rs[name] != ref:
                    raise ValueError('同一预制体骨骼冲突：%s / %s' % (owner, name))
                rs[name] = ref
            dest['renderers'] = sorted(rs.items())
    directory = os.path.dirname(os.path.abspath(out))
    fd, temp = tempfile.mkstemp(dir=directory, suffix='.tmp')
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as f:
            f.write('{"format":"gibox-offline-skeletons","version":1,"build":%s,"meshes":[\n' % json.dumps(build))
            f.write(',\n'.join(json.dumps(r, ensure_ascii=False, separators=(',', ':'), allow_nan=False) for r in records))
            f.write('\n],"prefabs":[\n')
            f.write(',\n'.join(json.dumps(prefabs[k], ensure_ascii=False, separators=(',', ':')) for k in sorted(prefabs)))
            f.write('\n]}\n')
        os.replace(temp, out)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    return len(records), len(prefabs)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+')
    parser.add_argument('-o', required=True)
    parser.add_argument('--build')
    a = parser.parse_args()
    n, p = merge(a.inputs, a.o, a.build)
    print('%d 条骨骼记录，%d 个预制体 -> %s' % (n, p, a.o))
