#!/usr/bin/env python3
"""
Locate XDK library functions (D3D8, DSound, Xapi, ...) inside default.xbe using
the signature tables of XbSymbolDatabase (read as data, nothing is compiled).

usage: xbsymscan.py <XbSymbolDatabase/src/OOVPADatabase> <default.xbe> [out.txt]

Output: one line per symbol, "0xADDRESS  Name  call=<conv> params=<n>".
"""

import os
import re
import struct
import sys

# ---------------------------------------------------------------- XBE --------

def load_xbe(path):
    x = open(path, 'rb').read()
    base = struct.unpack_from('<I', x, 0x104)[0]
    nsec, shdr = struct.unpack_from('<II', x, 0x11C)
    mem = bytearray(struct.unpack_from('<I', x, 0x10C)[0])
    mem[0:0x1000] = x[0:0x1000]
    code = []
    for i in range(nsec):
        fl, va, vs, ra, rs, nm = struct.unpack_from('<IIIIII', x, shdr - base + i * 56)
        need = va - base + max(vs, rs)
        if need > len(mem):
            mem.extend(bytes(need - len(mem)))
        mem[va - base:va - base + rs] = x[ra:ra + rs]
        name = x[nm - base:x.index(b'\0', nm - base)].decode()
        if fl & 4:  # executable
            code.append((va, va + vs, name))
    libs = {}
    nlib, plib = struct.unpack_from('<II', x, 0x160)
    for i in range(nlib):
        o = plib - base + i * 16
        libs[x[o:o + 8].rstrip(b'\0').decode()] = struct.unpack_from('<H', x, o + 12)[0]
    return base, bytes(mem), code, libs

# ------------------------------------------------------ signature tables -----

HDR = re.compile(r'(OOVPA_SIG_HEADER_(?:NO_)?XREF(?:_DETECT|_EXTEND)?|OOVPA_(?:NO_)?XREF(?:_DETECT|_EXTEND)?)\s*\(([^)]*)\)')

def strip_comments(s):
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return re.sub(r'//[^\n]*', '', s)

def num(t):
    t = t.strip()
    return int(t, 0)

def parse_inl(path, sigs):
    s = strip_comments(open(path, errors='replace').read())
    for m in HDR.finditer(s):
        args = [a.strip() for a in m.group(2).split(',')]
        name, ver = args[0], int(args[1])
        body_start = m.end()
        end = s.find('OOVPA_END', body_start)
        nxt = HDR.search(s, body_start)
        stop = min(e for e in (end if end >= 0 else len(s), nxt.start() if nxt else len(s)))
        body = s[body_start:stop]
        pairs, xrefs = [], []
        for xm in re.finditer(r'XREF_ENTRY\s*\(\s*([^,]+),\s*(\w+)\s*\)', body):
            xrefs.append((num(xm.group(1)), xm.group(2)))
        body2 = re.sub(r'XREF_ENTRY\s*\([^)]*\)', '', body)
        for om in re.finditer(r'OV_MATCH\s*\(([^)]*)\)', body2):
            try:
                v = [num(a) for a in om.group(1).split(',') if a.strip()]
            except ValueError:
                continue  # kernel-thunk / symbolic operand: wildcard
            for k, b in enumerate(v[1:]):
                pairs.append((v[0] + k, b))
        body3 = re.sub(r'OV_MATCH\s*\([^)]*\)', '', body2)
        for pm in re.finditer(r'\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}', body3):
            pairs.append((num(pm.group(1)), num(pm.group(2))))
        if pairs:
            sigs[(name, ver)] = (pairs, xrefs)

REG = re.compile(r'REGISTER_OOVPAS(?:_M|_C)?(?:_PREFIX)?(?:_(\w+?))?\s*\(\s*(?:(\w+)\s*,\s*)?SYM_(FUN|FUN_ALT|FUN_LTCG|VAR|INT)\s*\(\s*(\w+)')

def parse_table(path):
    """-> {name: dict(versions=[...], kind, call, nparams)}"""
    s = strip_comments(open(path, errors='replace').read())
    out = {}
    for m in REG.finditer(s):
        prefix = m.group(1) or m.group(2)
        if prefix and prefix in ('M', 'C'):
            prefix = None
        name = m.group(4)
        full = f'{prefix}_{name}' if prefix and prefix not in ('PREFIX',) else name
        # balanced parens to the end of this entry
        i, depth = m.start(), 0
        j = s.index('(', i)
        k = j
        while True:
            if s[k] == '(':
                depth += 1
            elif s[k] == ')':
                depth -= 1
                if depth == 0:
                    break
            k += 1
        entry = s[j:k]
        sm = re.search(r'SYM_SIG\s*\(([^)]*)\)', entry)
        vers = [int(v) for v in sm.group(1).split(',') if v.strip()] if sm else []
        cm = re.search(r'CALL\((\w+)\)', entry)
        npar = len(re.findall(r'PARAM\(', entry))
        out[full] = dict(versions=vers, kind=m.group(3), call=cm.group(1) if cm else '', nparams=npar)
    return out

# --------------------------------------------------------------- scan --------

def main():
    db, xbe_path = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else None
    base, mem, code, libs = load_xbe(xbe_path)
    print('libraries:', ', '.join(f'{k} {v}' for k, v in libs.items()), file=sys.stderr)

    libdirs = {'D3D8': 'D3D8', 'D3DX8': None, 'DSOUND': 'DSound', 'XAPILIB': 'Xapi', 'XGRAPHC': 'XGraphic',
               'XACTENG': 'XActEng', 'XNETS': 'XNet', 'XNET': 'XNet', 'XONLINE': 'XOnline', 'XONLINES': 'XOnline'}
    tables = {'D3D8': 'D3D8_OOVPA.c', 'DSound': 'DSound_OOVPA.c', 'Xapi': 'Xapi_OOVPA.c',
              'XGraphic': 'XGraphic_OOVPA.c', 'XActEng': 'XActEng_OOVPA.c', 'XNet': 'XNet_OOVPA.c',
              'XOnline': 'XOnline_OOVPA.c'}

    syms = []  # (name, info, [(pairs, xrefs), ...] newest revision first)
    for lib, ver in libs.items():
        d = libdirs.get(lib)
        if not d:
            continue
        sigs = {}
        for f in os.listdir(os.path.join(db, d)):
            if f.endswith('.inl'):
                parse_inl(os.path.join(db, d, f), sigs)
        table = parse_table(os.path.join(db, tables[d]))
        for name, info in table.items():
            cand = sorted(v for v in info['versions'] if v <= ver)
            # every revision up to the library's is a candidate: a revision's
            # signature often covers later builds too (newest tried first).
            # The .inl symbol name lacks the REGISTER prefix in some tables.
            revs = []
            for v in reversed(cand):
                for alt in (name, name.split('_', 1)[-1], 'D3D8_' + name, 'D3D_' + name):
                    if (alt, v) in sigs:
                        revs.append(sigs[(alt, v)])
                        break
            if revs:
                syms.append((name, info, revs))

    regions = [(lo - base, hi - base) for lo, hi, _ in code]
    found = {}       # name -> addr
    xref_val = {}    # xref name (without XREF_) -> addr

    def try_match(off, pairs, xrefs):
        for o, b in pairs:
            if mem[off + o] != b:
                return False
        for o, xn in xrefs:
            k = xn[5:] if xn.startswith('XREF_') else xn
            if k in xref_val:
                at = off + o
                absv = struct.unpack_from('<I', mem, at)[0]
                rel = (base + at + 4 + struct.unpack_from('<i', mem, at)[0]) & 0xFFFFFFFF
                if xref_val[k] not in (absv, rel):
                    return False
        return True

    def scan(pairs, xrefs):
        first_o, first_b = min(pairs)
        last = max(o for o, _ in pairs)
        hits = []
        for lo, hi in regions:
            pos = mem.find(bytes([first_b]), lo + first_o, hi)
            while pos != -1 and len(hits) < 2:
                off = pos - first_o
                if off + last < hi and try_match(off, pairs, xrefs):
                    hits.append(off)
                pos = mem.find(bytes([first_b]), pos + 1, hi)
        return hits

    for rnd in range(6):
        progress = False
        for name, info, revs in syms:
            if name in found:
                continue
            for pairs, xrefs in revs:
                if any((xn[5:] if xn.startswith('XREF_') else xn) not in xref_val for _, xn in xrefs) and rnd < 3:
                    # wait for dependencies, unless nothing else can make progress
                    if len(pairs) < 8:
                        continue
                hits = scan(pairs, xrefs)
                if len(hits) == 1:
                    break
            else:
                continue
            off = hits[0]
            found[name] = base + off
            xref_val.setdefault(name, base + off)
            for o, xn in xrefs:
                k = xn[5:] if xn.startswith('XREF_') else xn
                if k not in xref_val:
                    at = off + o
                    prev = mem[at - 1]
                    if prev in (0xE8, 0xE9) or (mem[at - 2] == 0x0F and 0x80 <= prev <= 0x8F):
                        xref_val[k] = (base + at + 4 + struct.unpack_from('<i', mem, at)[0]) & 0xFFFFFFFF
                    else:
                        xref_val[k] = struct.unpack_from('<I', mem, at)[0]
            progress = True
        if not progress and rnd >= 3:
            break

    lines = []
    for name, info, _ in syms:
        if name in found:
            lines.append(f"0x{found[name]:08x}  {name}  call={info['call']} params={info['nparams']}")
    lines.sort()
    for k, v in sorted(xref_val.items()):
        if k not in found:
            lines.append(f"0x{v:08x}  {k}  xref")
    txt = '\n'.join(lines) + '\n'
    if out_path:
        open(out_path, 'w').write(txt)
    print(f'{len(found)} of {len(syms)} symbols located', file=sys.stderr)
    if not out_path:
        sys.stdout.write(txt)


if __name__ == '__main__':
    main()
