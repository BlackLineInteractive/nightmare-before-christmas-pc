#!/usr/bin/env python3
"""
Complete Flat PE Generator & Hardware Patcher for Capcom RD3 'Oogie's Revenge'
Generates:
1. rom/extracted/default_pc.exe (flat PE with 1:1 memory mapping of default.xbe)

The Xbox kernel is not needed: XAPI is pointed at Windows kernel32.dll
(see tools/xapi_port.py), the only imports are kernel32.dll and ntdll.dll.
"""

import os
import sys
import struct

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xapi_port
import pc_render

def align(val, alignment):
    return (val + alignment - 1) & ~(alignment - 1)

def build_flat_binary(xbe_path, pe_path):
    print(f"[*] Reading Xbox binary: {xbe_path}")
    with open(xbe_path, 'rb') as f:
        xbe = f.read()

    base_addr = struct.unpack('<I', xbe[0x104:0x108])[0]         # 0x00010000
    size_of_headers = struct.unpack('<I', xbe[0x108:0x10c])[0]   # 0x00002000
    size_of_image = struct.unpack('<I', xbe[0x10c:0x110])[0]     # 0x00421000
    entry_point_va = struct.unpack('<I', xbe[0x128:0x12c])[0] ^ 0xA8FC57AB # Retail key decrypt
    sec_count = struct.unpack('<I', xbe[0x11c:0x120])[0]
    sec_hdrs_addr = struct.unpack('<I', xbe[0x120:0x124])[0]

    sec_off = sec_hdrs_addr - base_addr
    max_va = base_addr + size_of_image
    sections = []
    for i in range(sec_count):
        sh = xbe[sec_off + i*56 : sec_off + (i+1)*56]
        flags, vaddr, vsize, raddr, rsize, name_addr = struct.unpack('<IIIIII', sh[:24])
        sec_end = vaddr + max(vsize, rsize)
        if sec_end > max_va:
            max_va = sec_end

        name = ''
        noff = name_addr - base_addr
        while xbe[noff] != 0:
            name += chr(xbe[noff])
            noff += 1

        sections.append({
            'name': name,
            'vaddr': vaddr,
            'vsize': vsize,
            'raddr': raddr,
            'rsize': rsize
        })

    # Flat memory span aligned to 4KB
    total_span = align(max_va - base_addr, 0x1000)
    print(f"[*] Total Flat Memory Span: {total_span:,} bytes (ends at VA 0x{base_addr + total_span:08x})")

    # Allocate flat memory
    image_mem = bytearray(total_span)

    # 1. Copy initial XBE headers at 0x0000..0x2000
    hdr_len = min(len(xbe), size_of_headers)
    image_mem[0:hdr_len] = xbe[0:hdr_len]

    # 2. Map all sections into their exact virtual addresses
    for s in sections:
        mem_dest = s['vaddr'] - base_addr
        copy_len = min(s['rsize'], len(xbe) - s['raddr'])
        if copy_len > 0:
            image_mem[mem_dest : mem_dest + copy_len] = xbe[s['raddr'] : s['raddr'] + copy_len]

    # 3. Apply the 7 Hardware Patches directly into flat memory
    # Patch 1: D3D_WaitGPUReady spinlock at VA 0x001dc611 -> xor eax, eax; ret; nop
    p1_off = 0x001dc611 - base_addr
    image_mem[p1_off : p1_off + 4] = b'\x31\xC0\xC3\x90'
    print(f"[+] [PATCH 1] Neutralized D3D_WaitGPUReady at VA 0x001dc611")

    # Patch 2: NV2A device init at VA 0x001dc2d1 -> xor eax, eax; inc eax; ret; nop...
    p2_off = 0x001dc2d1 - base_addr
    image_mem[p2_off : p2_off + 4] = b'\x31\xC0\x40\xC3'
    for i in range(4, 25):
        image_mem[p2_off + i] = 0x90
    print(f"[+] [PATCH 2] Neutralized NV2A device init at VA 0x001dc2d1")

    # Patch 3: NV2A chip detection at VA 0x001dc2fc -> mov eax, 0x00fe502a; ret
    p3_off = 0x001dc2fc - base_addr
    image_mem[p3_off : p3_off + 6] = b'\xB8\x2A\x50\xFE\x00\xC3'
    print(f"[+] [PATCH 3] Neutralized NV2A chip architecture detection at VA 0x001dc2fc")

    # Patch 4: PMC hardware status reads (0xFD40014C) at 0x1dbc1d, 0x1dbe5f, 0x1dc00f
    for pmc_va in [0x001dbc1d, 0x001dbe5f, 0x001dc00f]:
        pmc_off = pmc_va - base_addr
        image_mem[pmc_off : pmc_off + 6] = b'\x31\xC0\x90\x90\x90\x90'
        print(f"[+] [PATCH 4] Neutralized 0xFD40014C read at VA 0x{pmc_va:08x}")

    # Patch 5: XPP USB MMIO write at VA 0x0025c41f -> 5 NOPs
    p5_off = 0x0025c41f - base_addr
    image_mem[p5_off : p5_off + 5] = b'\x90\x90\x90\x90\x90'
    print(f"[+] [PATCH 5] Neutralized XPP MMIO write at VA 0x0025c41f")

    # 4. XAPI -> Windows (tools/xapi_port.py): XAPI functions jump into
    #    kernel32.dll, kernel thunks still used by LIBC go to ntdll/kernel32,
    #    KPCR / TLS accesses use the Windows TEB, paths are made relative.
    section_alignment = 0x1000
    file_alignment = 0x1000
    flat_sec_vaddr = 0x1000
    flat_sec_vsize = total_span - flat_sec_vaddr

    kernel_names = read_kernel_thunks(image_mem, base_addr)
    port_rva = align(total_span, section_alignment)
    renderer = (pc_render.compile_renderer(), pc_render.PC_TABLE, pc_render.dll_of)
    port, info = xapi_port.build_port_section(image_mem, base_addr, port_rva, kernel_names, renderer)
    st = info['stats']
    print(f"[+] XAPI: {st['xapi']} functions -> kernel32, {st['stubs']} X* replacements")
    print(f"[+] Kernel thunks: {st['bound']} -> Windows, {st['data']} data, {st['traps']} unported (trap)")
    print(f"[+] D3D + XPP: {st['d3d']} entry points -> PC renderer / input (src/PC)")
    print(f"[+] Paths: host0: builder + {st['paths']} drive strings -> .\\; KPCR sites: {st['kpcr']}; static critical sections: {st['cs']}")

    # 5. PE headers: .flat (XBE 1:1) + .port
    out_secs = [
        dict(name='.flat', rva=flat_sec_vaddr, data=image_mem[flat_sec_vaddr:total_span], flags=0xE0000060),
        dict(name='.port', rva=port_rva, data=port, flags=0xE0000060),
    ]
    raw = 0x1000
    for sec in out_secs:
        sec['vsize'] = align(len(sec['data']), section_alignment)
        sec['rsize'] = align(len(sec['data']), file_alignment)
        sec['raw'] = raw
        raw += sec['rsize']
    total_image_size = port_rva + out_secs[1]['vsize']

    dos_hdr = bytearray(64)
    dos_hdr[0:2] = b'MZ'
    pe_hdr_offset = 0xC00
    struct.pack_into('<I', dos_hdr, 0x3C, pe_hdr_offset)

    coff_hdr = struct.pack('<HHIIIHH', 0x014c, len(out_secs), 0, 0, 0, 224,
                           0x0123)  # RELOCS_STRIPPED | EXECUTABLE | LARGE_ADDRESS_AWARE | 32BIT
    opt_hdr = struct.pack(
        '<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII',
        0x010b, 7, 10,
        out_secs[0]['rsize'] + out_secs[1]['rsize'], 0, 0,
        info['entry_rva'],
        flat_sec_vaddr, flat_sec_vaddr,
        base_addr, section_alignment, file_alignment,
        4, 0, 0, 0, 4, 0, 0,
        total_image_size, 0x1000, 0,
        3,          # Subsystem = Console (CUI)
        0x0000,     # DllCharacteristics
        0x200000, 0x20000, 0x200000, 0x10000,
        0, 16
    )
    data_dirs = bytearray(16 * 8)
    struct.pack_into('<II', data_dirs, 1 * 8, *info['import_dir'])
    struct.pack_into('<II', data_dirs, 9 * 8, *info['tls_dir'])
    struct.pack_into('<II', data_dirs, 12 * 8, *info['iat_dir'])

    header_block = bytearray(0x1000)
    header_block[0:64] = dos_hdr
    header_block[64:pe_hdr_offset] = xbe[64:pe_hdr_offset]
    hdr = b'PE\x00\x00' + coff_hdr + opt_hdr + data_dirs
    for sec in out_secs:
        hdr += struct.pack('<8sIIIIIIHHI', sec['name'].encode(), sec['vsize'], sec['rva'],
                           sec['rsize'], sec['raw'], 0, 0, 0, 0, sec['flags'])
    header_block[pe_hdr_offset : pe_hdr_offset + len(hdr)] = hdr

    with open(pe_path, 'wb') as out_f:
        out_f.write(header_block)
        for sec in out_secs:
            out_f.write(sec['data'])
            out_f.write(b'\x00' * (sec['rsize'] - len(sec['data'])))

    print(f"[+] Successfully generated patched PE: '{pe_path}' ({os.path.getsize(pe_path):,} bytes)")
    print(f"    entry 0x{base_addr + info['entry_rva']:08x} -> _rtinit, _cinit, game main 0x{xapi_port.GAME_MAIN:08x}")


def read_kernel_thunks(image_mem, base_addr):
    """[(thunk VA, ordinal, name)] for the XBE's xboxkrnl.exe thunk table."""
    out = []
    va = xapi_port.KERNEL_THUNKS_VA
    while True:
        val = struct.unpack_from('<I', image_mem, va - base_addr)[0]
        if val == 0:
            break
        o = val & 0x7fffffff
        out.append((va, o, xapi_port.KERNEL_ORDINAL_NAMES[o]))
        va += 4
    return out


if __name__ == '__main__':
    xbe = "rom/extracted/default.xbe"
    pe = "rom/extracted/default_pc.exe"
    build_flat_binary(xbe, pe)
