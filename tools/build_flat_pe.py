#!/usr/bin/env python3
"""
Complete Flat PE Generator & Hardware Patcher for Capcom RD3 'Oogie's Revenge'
Generates rom/extracted/default_pc.exe: a flat PE with 1:1 memory mapping of
default.xbe. The Xbox kernel (src/XboxKrnl) is compiled and linked into the
exe itself; the only imports are kernel32.dll and ntdll.dll.
"""

import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coff_link

KERNEL_SRC = "src/XboxKrnl/xboxkrnl.c"
KERNEL_DEF = "src/XboxKrnl/xboxkrnl.def"
KERNEL_OBJ = "build/xboxkrnl/xboxkrnl.o"

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

    # 3. Hardware MMIO (NV2A at 0xFD000000, USB/APU above it) is left intact:
    # xboxkrnl.dll maps those physical windows as memory, so the XDK code
    # runs unmodified. The old byte patches are kept behind --nv2a-patches.
    if '--nv2a-patches' in sys.argv:
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

    # Redirect Xbox KPCR accesses (fs:) onto unused TEB UserReserved slots that
    # xboxkrnl.dll fills per thread. Same instruction lengths, only the
    # displacement changes, so game logic is untouched.
    #   fs:[0x04] StackBase (TLS lookup [StackBase + idx*4]) -> fs:[0xAC]
    #   fs:[0x20] Prcb                                       -> fs:[0xB0]
    #   fs:[0x28] Prcb.CurrentThread                         -> fs:[0xB4]
    #   fs:[0x58] Prcb.DpcRoutineActive                      -> fs:[0xB8]
    #   fs:[0x24] Irql                                       -> fs:[0xBC]
    fs_patterns = [
        (b'\x64\xa1\x20\x00\x00\x00',         b'\x64\xa1\xb0\x00\x00\x00',         10, "mov eax, fs:[0x20]"),
        (b'\x64\xa1\x28\x00\x00\x00',         b'\x64\xa1\xb4\x00\x00\x00',          9, "mov eax, fs:[0x28]"),
        (b'\x64\xa1\x58\x00\x00\x00',         b'\x64\xa1\xb8\x00\x00\x00',          2, "mov eax, fs:[0x58]"),
        (b'\x64\x8b\x0d\x04\x00\x00\x00',     b'\x64\x8b\x0d\xac\x00\x00\x00',      5, "mov ecx, fs:[0x04]"),
        (b'\x64\x8b\x3d\x04\x00\x00\x00',     b'\x64\x8b\x3d\xac\x00\x00\x00',      1, "mov edi, fs:[0x04]"),
        (b'\x64\x0f\xb6\x05\x24\x00\x00\x00', b'\x64\x0f\xb6\x05\xbc\x00\x00\x00', 12, "movzx eax, byte fs:[0x24]"),
    ]
    # Counts were verified against a disassembly of every code section: each
    # hit is a real instruction, none are in data.
    for pat, repl, expected, desc in fs_patterns:
        hits = []
        pos = image_mem.find(pat)
        while pos != -1:
            hits.append(pos)
            pos = image_mem.find(pat, pos + 1)
        if len(hits) != expected:
            raise SystemExit(f"[!] {desc}: expected {expected} sites, found {len(hits)}")
        for idx in hits:
            image_mem[idx : idx + len(repl)] = repl
        print(f"[+] [FS PATCH] {desc}: {len(hits)} sites redirected to TEB")

    # 4. Link the Xbox kernel (src/XboxKrnl) into the image right after the
    #    flat XBE span, and point the XBE's kernel thunks straight at it.
    section_alignment = 0x1000
    file_alignment = 0x1000
    flat_sec_vaddr = 0x1000
    flat_sec_vsize = total_span - flat_sec_vaddr

    kobj = compile_kernel()
    kernel = coff_link.link_object(kobj, base_addr, align(total_span, section_alignment), section_alignment)
    ksyms = kernel['symbols']

    def kva(sym):
        if sym not in ksyms:
            raise SystemExit(f"[!] kernel symbol missing: {sym} (check stdcall arg sizes)")
        return base_addr + ksyms[sym]

    iat_va = 0x00260020
    iat_off = iat_va - base_addr
    impl = {o: coff_link.c_symbol(n) for n, o in coff_link.parse_def(KERNEL_DEF)}
    k = 0
    while True:
        val = struct.unpack_from('<I', image_mem, iat_off + k * 4)[0]
        if val == 0:
            break
        struct.pack_into('<I', image_mem, iat_off + k * 4, kva(impl[val & 0x7fffffff]))
        k += 1
    print(f"[+] Bound {k} Xbox kernel thunks at VA 0x{iat_va:08x} to the in-exe kernel")

    # XBE entry point and the original header page the PE headers displace
    kdata = next(sec for sec in kernel['sections'] if sec['name'] == '.kdata')
    def kdata_put(sym, blob):
        off = ksyms[sym] - kdata['rva']
        kdata['data'][off : off + len(blob)] = blob
    kdata_put('_g_xbeEntry', struct.pack('<I', entry_point_va))
    kdata_put('_g_xbeHeader', xbe[0:0x1000])
    entry_rva = ksyms['_xb_ExeEntry']

    # 5. PE headers: .flat (XBE 1:1) + kernel sections
    out_secs = [dict(name='.flat', rva=flat_sec_vaddr, data=image_mem[flat_sec_vaddr:total_span],
                     flags=0xE0000060)] + kernel['sections']
    for sec in out_secs:
        sec['vsize'] = align(max(len(sec['data']), 1), section_alignment)
        sec['rsize'] = align(len(sec['data']), file_alignment)
    raw = 0x1000
    for sec in out_secs:
        sec['raw'] = raw
        raw += sec['rsize']
    total_image_size = kernel['end_rva']
    code_size = sum(sec['rsize'] for sec in out_secs if sec['flags'] & 0x20)
    data_size = sum(sec['rsize'] for sec in out_secs if not sec['flags'] & 0x20)

    dos_hdr = bytearray(64)
    dos_hdr[0:2] = b'MZ'
    pe_hdr_offset = 0xC00
    struct.pack_into('<I', dos_hdr, 0x3C, pe_hdr_offset)

    coff_hdr = struct.pack('<HHIIIHH', 0x014c, len(out_secs), 0, 0, 0, 224,
                           0x0123)  # RELOCS_STRIPPED | EXECUTABLE | LARGE_ADDRESS_AWARE | 32BIT
    opt_hdr = struct.pack(
        '<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII',
        0x010b, 7, 10,
        code_size, data_size, 0,
        entry_rva,  # EntryPoint -> xb_ExeEntry (kernel bring-up, then XBE entry)
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
    struct.pack_into('<II', data_dirs, 1 * 8, *kernel['import_dir'])

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
    print(f"    entry 0x{base_addr + entry_rva:08x} (xb_ExeEntry) -> XBE entry 0x{entry_point_va:08x}")


def compile_kernel():
    """clang -c src/XboxKrnl/xboxkrnl.c -> i386 COFF object."""
    os.makedirs(os.path.dirname(KERNEL_OBJ), exist_ok=True)
    # Any clang with the x86 backend works, Apple's included
    clang = os.environ.get('CLANG', 'clang')
    subprocess.run([clang, '--target=i686-pc-windows-gnu', '-c', '-O2', '-ffreestanding',
                    '-fno-stack-protector', '-fno-asynchronous-unwind-tables', '-fno-unwind-tables',
                    '-fno-ident', '-mno-stack-arg-probe', '-Wall', '-Wno-unused-function',
                    KERNEL_SRC, '-o', KERNEL_OBJ], check=True)
    return KERNEL_OBJ


if __name__ == '__main__':
    xbe = "rom/extracted/default.xbe"
    pe = "rom/extracted/default_pc.exe"
    build_flat_binary(xbe, pe)
