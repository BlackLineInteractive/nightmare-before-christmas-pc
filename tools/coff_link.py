#!/usr/bin/env python3
"""
Minimal i386 COFF linker used by build_flat_pe.py to place the in-exe Xbox
kernel (src/XboxKrnl/xboxkrnl.c) into the patched default_pc.exe image:
  - merges .text / .rdata / .data / .bss into sections at a given RVA
  - resolves DIR32 / REL32 / DIR32NB relocations for a fixed image base
  - builds the import table for __imp__ symbols (kernel32 / ntdll)

No mingw / lld needed: clang -c produces the object, this does the rest.
"""

import struct

NTDLL_PREFIXES = ('Nt', 'Rtl')


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def read_coff(path):
    d = open(path, 'rb').read()
    machine, nsec, _, symptr, nsyms, opthdr, _ = struct.unpack_from('<HHIIIHH', d, 0)
    assert machine == 0x14C, 'not an i386 COFF object'
    strtab_off = symptr + nsyms * 18

    def name_of(raw):
        if raw[:4] == b'\0\0\0\0':
            off = struct.unpack_from('<I', raw, 4)[0]
            end = d.index(b'\0', strtab_off + off)
            return d[strtab_off + off:end].decode()
        return raw.rstrip(b'\0').decode()

    sections = []
    for i in range(nsec):
        o = 20 + opthdr + i * 40
        raw = d[o:o + 8]
        vsize, vaddr, rsize, rptr, relptr, _, nrel, _, ch = struct.unpack_from('<IIIIIIHHI', d, o + 8)
        if raw[0:1] == b'/':
            name = d[strtab_off + int(raw[1:].rstrip(b'\0')):].split(b'\0')[0].decode()
        else:
            name = name_of(raw)
        relocs = [struct.unpack_from('<IIH', d, relptr + r * 10) for r in range(nrel)]
        data = d[rptr:rptr + rsize] if rptr else bytes(rsize)
        al = 1 << (((ch >> 20) & 0xF) - 1) if (ch >> 20) & 0xF else 16
        sections.append(dict(name=name, data=bytearray(data), size=rsize, relocs=relocs,
                             align=al, bss=bool(ch & 0x80)))

    symbols = {}
    i = 0
    while i < nsyms:
        o = symptr + i * 18
        value, secnum, _, scl, naux = struct.unpack_from('<IhHBB', d, o + 8)
        symbols[i] = dict(name=name_of(d[o:o + 8]), value=value, sec=secnum, scl=scl)
        i += 1 + naux
    return sections, symbols


def group_of(sec):
    n = sec['name']
    if n.startswith('.text'):
        return '.ktext'
    if n.startswith('.rdata'):
        return '.krdata'
    if n.startswith('.data') or n.startswith('.bss') or sec['bss']:
        return '.kdata'
    return None  # debug, drectve, addrsig, ...


def link_object(obj, image_base, start_rva, sect_align=0x1000):
    """Link obj at image_base + start_rva.

    Returns dict(sections=[{name, rva, data, flags}], symbols={name: rva},
                 import_dir=(rva, size), end_rva=next free aligned rva)
    """
    sections, symbols = read_coff(obj)

    imports = {}  # dll -> [func]
    for sym in symbols.values():
        if sym['sec'] == 0 and sym['name'].startswith('__imp__'):
            base = sym['name'][len('__imp__'):].split('@')[0]
            dll = 'ntdll.dll' if base.startswith(NTDLL_PREFIXES) else 'kernel32.dll'
            if base not in imports.setdefault(dll, []):
                imports[dll].append(base)

    rva = start_rva
    out = []
    sec_rva = {}
    flags = {'.ktext': 0x60000020, '.krdata': 0x40000040, '.kdata': 0xC0000040}
    for gname in ('.ktext', '.krdata', '.kdata'):
        idxs = [i for i, s in enumerate(sections) if group_of(s) == gname]
        idxs.sort(key=lambda i: sections[i]['bss'])  # initialised data before bss
        buf = bytearray()
        for i in idxs:
            s = sections[i]
            buf.extend(b'\0' * (align(len(buf), s['align']) - len(buf)))
            sec_rva[i] = rva + len(buf)
            buf.extend(b'\0' * s['size'] if s['bss'] else s['data'])
        out.append(dict(name=gname, rva=rva, data=buf, flags=flags[gname]))
        rva = align(rva + max(len(buf), 1), sect_align)

    # --- import section ---
    idata_rva = rva
    dlls = sorted(imports)
    desc_size = (len(dlls) + 1) * 20
    idata = bytearray(desc_size)
    iat_slots = {}
    layout = []
    for dll in dlls:
        n = len(imports[dll])
        ilt = len(idata); idata.extend(b'\0' * ((n + 1) * 4))
        iat = len(idata); idata.extend(b'\0' * ((n + 1) * 4))
        layout.append((dll, ilt, iat))
        for k, f in enumerate(imports[dll]):
            iat_slots[f] = idata_rva + iat + k * 4
    for di, (dll, ilt, iat) in enumerate(layout):
        for k, f in enumerate(imports[dll]):
            if len(idata) % 2:
                idata.append(0)
            hn = idata_rva + len(idata)
            idata.extend(b'\0\0' + f.encode() + b'\0')
            struct.pack_into('<I', idata, ilt + k * 4, hn)
            struct.pack_into('<I', idata, iat + k * 4, hn)
        name_rva = idata_rva + len(idata)
        idata.extend(dll.encode() + b'\0')
        struct.pack_into('<IIIII', idata, di * 20, idata_rva + ilt, 0, 0, name_rva, idata_rva + iat)
    out.append(dict(name='.kidata', rva=idata_rva, data=idata, flags=0xC0000040))
    rva = align(idata_rva + len(idata), sect_align)

    # --- symbols ---
    globals_by_name = {}
    for sym in symbols.values():
        if sym['sec'] > 0 and sym['scl'] == 2 and (sym['sec'] - 1) in sec_rva:
            globals_by_name[sym['name']] = sec_rva[sym['sec'] - 1] + sym['value']

    def sym_rva(sym):
        if sym['sec'] > 0:
            if (sym['sec'] - 1) not in sec_rva:
                raise SystemExit(f"symbol {sym['name']} in discarded section")
            return sec_rva[sym['sec'] - 1] + sym['value']
        if sym['name'].startswith('__imp__'):
            return iat_slots[sym['name'][len('__imp__'):].split('@')[0]]
        if sym['name'] in globals_by_name:
            return globals_by_name[sym['name']]
        raise SystemExit(f"undefined symbol: {sym['name']}")

    # --- relocations (fixed base: no .reloc emitted) ---
    by_name = {s['name']: s for s in out}
    for i, s in enumerate(sections):
        if i not in sec_rva:
            continue
        target = by_name[group_of(s)]
        for (va, symidx, typ) in s['relocs']:
            p = sec_rva[i] + va
            off = p - target['rva']
            S = sym_rva(symbols[symidx])
            cur = struct.unpack_from('<i', target['data'], off)[0]
            if typ == 0x06:      # DIR32
                v = cur + image_base + S
            elif typ == 0x14:    # REL32
                v = cur + S - (p + 4)
            elif typ == 0x07:    # DIR32NB
                v = cur + S
            else:
                raise SystemExit(f'unsupported reloc type {typ:#x} in {s["name"]}')
            struct.pack_into('<I', target['data'], off, v & 0xFFFFFFFF)

    return dict(sections=out, symbols=globals_by_name, import_dir=(idata_rva, desc_size), end_rva=rva)


def parse_def(path):
    """Parse 'Name @ ordinal NONAME [DATA]' lines -> [(name, ordinal)]."""
    exports = []
    for line in open(path):
        line = line.split(';')[0].strip()
        if not line or line.upper().startswith(('LIBRARY', 'EXPORTS')):
            continue
        parts = line.split()
        exports.append((parts[0], int(parts[parts.index('@') + 1])))
    return exports


def c_symbol(export_name):
    """xboxkrnl export name -> C symbol implementing it (xb_ prefix)."""
    if export_name.startswith('@'):          # fastcall: @Name@N
        return '@xb_' + export_name[1:]
    return '_xb_' + export_name
