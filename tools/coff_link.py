"""
Minimal i386 COFF linker for build_flat_pe.py: places one clang-compiled
object (the PC renderer, src/PC) at a fixed address inside the .port section.

  - merges .text / .rdata / .data / .bss into one blob
  - resolves DIR32 / REL32 / DIR32NB relocations for the fixed image base
  - __imp__Name@N symbols resolve to IAT slots supplied by the caller
"""

import struct


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def read_coff(path):
    d = open(path, 'rb').read()
    machine, nsec, _, symptr, nsyms, opthdr, _ = struct.unpack_from('<HHIIIHH', d, 0)
    assert machine == 0x14C, 'not an i386 COFF object'
    strtab = symptr + nsyms * 18

    def name_of(raw):
        if raw[:4] == b'\0\0\0\0':
            off = strtab + struct.unpack_from('<I', raw, 4)[0]
            return d[off:d.index(b'\0', off)].decode()
        return raw.rstrip(b'\0').decode()

    sections = []
    for i in range(nsec):
        o = 20 + opthdr + i * 40
        raw = d[o:o + 8]
        _, _, rsize, rptr, relptr, _, nrel, _, ch = struct.unpack_from('<IIIIIIHHI', d, o + 8)
        if raw[:1] == b'/':
            off = strtab + int(raw[1:].rstrip(b'\0'))
            name = d[off:d.index(b'\0', off)].decode()
        else:
            name = name_of(raw)
        al = 1 << (((ch >> 20) & 0xF) - 1) if (ch >> 20) & 0xF else 16
        sections.append(dict(name=name, size=rsize, align=al, bss=bool(ch & 0x80),
                             data=bytearray(d[rptr:rptr + rsize]) if rptr else bytearray(rsize),
                             relocs=[struct.unpack_from('<IIH', d, relptr + r * 10) for r in range(nrel)]))
    symbols = {}
    i = 0
    while i < nsyms:
        o = symptr + i * 18
        value, secnum, _, scl, naux = struct.unpack_from('<IhHBB', d, o + 8)
        symbols[i] = dict(name=name_of(d[o:o + 8]), value=value, sec=secnum, scl=scl)
        i += 1 + naux
    return sections, symbols


def keep(sec):
    n = sec['name']
    return n.startswith(('.text', '.rdata', '.data', '.bss')) or sec['bss']


def imports_of(path):
    """['Name', ...] of __imp__ symbols the object needs (decoration removed)."""
    _, symbols = read_coff(path)
    out = []
    for s in symbols.values():
        if s['sec'] == 0 and s['name'].startswith('__imp__'):
            n = s['name'][len('__imp__'):].split('@')[0]
            if n not in out:
                out.append(n)
    return out


def link(path, va, iat):
    """Link the object at absolute address va. iat: {Name: IAT slot VA}.

    Returns (blob, {symbol: VA}).
    """
    sections, symbols = read_coff(path)
    order = sorted((i for i, s in enumerate(sections) if keep(s)),
                   key=lambda i: (sections[i]['bss'], not sections[i]['name'].startswith('.text')))
    blob = bytearray()
    at = {}
    for i in order:
        s = sections[i]
        blob += bytes(align(len(blob), s['align']) - len(blob))
        at[i] = len(blob)
        blob += bytes(s['size']) if s['bss'] else s['data']

    def sym_va(s):
        if s['sec'] > 0:
            if s['sec'] - 1 not in at:
                raise SystemExit(f"[!] {path}: symbol {s['name']} in a discarded section")
            return va + at[s['sec'] - 1] + s['value']
        if s['name'].startswith('__imp__'):
            return iat[s['name'][len('__imp__'):].split('@')[0]]
        if s['name'] in defined:
            return defined[s['name']]
        raise SystemExit(f"[!] {path}: undefined symbol {s['name']}")

    defined = {}
    for s in symbols.values():
        if s['sec'] > 0 and s['scl'] == 2 and s['sec'] - 1 in at:
            defined[s['name']] = va + at[s['sec'] - 1] + s['value']

    for i in order:
        for (off, symidx, typ) in sections[i]['relocs']:
            p = at[i] + off
            S = sym_va(symbols[symidx])
            cur = struct.unpack_from('<i', blob, p)[0]
            if typ == 0x06:      # DIR32
                v = cur + S
            elif typ == 0x14:    # REL32
                v = cur + S - (va + p + 4)
            elif typ == 0x07:    # DIR32NB
                v = cur + S - va
            else:
                raise SystemExit(f'[!] unsupported relocation type {typ:#x}')
            struct.pack_into('<I', blob, p, v & 0xFFFFFFFF)
    return blob, defined
