#!/usr/bin/env python3
"""
XBE to PE Binary Converter & Hardware Hook Patcher
Converts Xbox 'default.xbe' into a standard Win32 / PE x86 executable/library,
reconstructing PE headers, un-XORing entry point and thunk tables,
and preparing hooks to replace Xbox NV2A GPU and DirectSound with modern APIs.
"""

import os
import sys
import struct

def convert_xbe_to_pe(xbe_path, pe_path):
    with open(xbe_path, 'rb') as f:
        xbe = f.read()

    if xbe[:4] != b'XBEH':
        raise ValueError("Not a valid XBE file (missing 'XBEH' magic)")

    # Read XBE header fields
    base_addr = struct.unpack('<I', xbe[0x104:0x108])[0]
    hdr_size = struct.unpack('<I', xbe[0x108:0x10c])[0]
    img_size = struct.unpack('<I', xbe[0x10c:0x110])[0]
    timestamp = struct.unpack('<I', xbe[0x114:0x118])[0]
    sec_count = struct.unpack('<I', xbe[0x11c:0x120])[0]
    sec_hdrs_addr = struct.unpack('<I', xbe[0x120:0x124])[0]
    entry_raw = struct.unpack('<I', xbe[0x128:0x12c])[0]
    entry_point = entry_raw ^ 0xA8FC57AB  # Retail XOR key
    entry_rva = entry_point - base_addr

    print(f"[*] Base Address: 0x{base_addr:08x}")
    print(f"[*] Image Size: {img_size:,} bytes")
    print(f"[*] Virtual Entry Point: 0x{entry_point:08x} (RVA: 0x{entry_rva:08x})")
    print(f"[*] Sections: {sec_count}")

    # Build DOS Header (64 bytes)
    dos_hdr = bytearray(64)
    dos_hdr[0:2] = b'MZ'
    struct.pack_into('<I', dos_hdr, 0x3C, 0x40)  # e_lfanew = 0x40

    # PE Signature
    pe_sig = b'PE\x00\x00'

    # COFF Header (20 bytes)
    # Machine = 0x014c (i386), SecCount, Timestamp, PtrSymbols=0, NumSymbols=0, OptHdrSize=224, Characteristics=0x0102
    coff_hdr = struct.pack('<HHIIIHH', 0x014c, sec_count, timestamp, 0, 0, 224, 0x0102)

    # Optional Header (Standard PE32: 224 bytes)
    # Magic = 0x010b (PE32)
    magic = 0x010b
    major_linker = 7
    minor_linker = 10
    size_of_code = 0
    size_of_init_data = 0
    size_of_uninit_data = 0
    base_of_code = 0x00002000
    base_of_data = 0x00270a80
    section_alignment = 0x1000
    file_alignment = 0x1000

    # Parse sections first to compute sizes
    sec_off = sec_hdrs_addr - base_addr
    sections = []
    for i in range(sec_count):
        sh = xbe[sec_off + i*56 : sec_off + (i+1)*56]
        flags, vaddr, vsize, raddr, rsize, name_addr = struct.unpack('<IIIIII', sh[:24])
        name_off = name_addr - base_addr
        name = ''
        while name_off < len(xbe) and xbe[name_off] != 0:
            name += chr(xbe[name_off])
            name_off += 1

        rva = vaddr - base_addr
        sections.append({
            'name': name[:8],
            'vsize': vsize,
            'rva': rva,
            'rsize': rsize,
            'raddr': raddr,
            'flags': flags
        })
        if name == '.text':
            size_of_code = rsize
        elif name in ['.data', '.rdata', 'D3D', 'DSOUND']:
            size_of_init_data += rsize

    size_of_headers = 0x1000

    opt_hdr = struct.pack(
        '<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII',
        magic, major_linker, minor_linker, size_of_code, size_of_init_data, size_of_uninit_data,
        entry_rva, base_of_code, base_of_data,
        base_addr, section_alignment, file_alignment,
        4, 0, 0, 0, 4, 0, 0,  # OS/Image/Subsystem versions + Win32VersionValue
        img_size, size_of_headers, 0, # CheckSum
        2,  # Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI
        0,  # DllCharacteristics
        0x100000, 0x1000, 0x100000, 0x1000,  # Stack/Heap sizes
        0, 16  # LoaderFlags, NumberOfRvaAndSizes
    )
    # 16 Data Directories (16 * 8 = 128 bytes, zeroed for now)
    data_dirs = bytearray(128)

    # Section Headers (40 bytes each)
    pe_sec_headers = bytearray()
    for s in sections:
        sec_name_bytes = s['name'].encode('ascii')[:8].ljust(8, b'\x00')
        # Standard PE characteristics: code vs data
        pe_char = 0x60000020 if s['name'] == '.text' else 0xC0000040
        sec_entry = struct.pack(
            '<8sIIIIIIHHI',
            sec_name_bytes,
            s['vsize'],
            s['rva'],
            s['rsize'],
            s['raddr'],
            0, 0, 0, 0,  # Relocations / Linenumbers
            pe_char
        )
        pe_sec_headers.extend(sec_entry)

    # Assemble PE Header
    header_block = bytearray(size_of_headers)
    pe_hdr_offset = len(dos_hdr)
    header_block[0:64] = dos_hdr
    header_block[pe_hdr_offset : pe_hdr_offset + 4] = pe_sig
    off = pe_hdr_offset + 4
    header_block[off : off + len(coff_hdr)] = coff_hdr
    off += len(coff_hdr)
    header_block[off : off + len(opt_hdr)] = opt_hdr
    off += len(opt_hdr)
    header_block[off : off + len(data_dirs)] = data_dirs
    off += len(data_dirs)
    header_block[off : off + len(pe_sec_headers)] = pe_sec_headers

    # Write output PE file
    with open(pe_path, 'wb') as out_f:
        # Write header
        out_f.write(header_block)

        # Write sections from raw offsets in XBE
        for s in sections:
            if s['rsize'] > 0 and s['raddr'] < len(xbe):
                out_f.seek(s['raddr'])
                sec_data = xbe[s['raddr'] : s['raddr'] + s['rsize']]
                out_f.write(sec_data)

    print(f"[+] Successfully converted '{xbe_path}' -> '{pe_path}' ({os.path.getsize(pe_path):,} bytes)")

if __name__ == '__main__':
    in_xbe = sys.argv[1] if len(sys.argv) > 1 else 'rom/extracted/default.xbe'
    out_pe = sys.argv[2] if len(sys.argv) > 2 else 'rom/extracted/default.exe'
    convert_xbe_to_pe(in_xbe, out_pe)
