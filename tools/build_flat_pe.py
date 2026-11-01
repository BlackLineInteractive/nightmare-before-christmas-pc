#!/usr/bin/env python3
"""
Complete Flat PE Generator & Hardware Patcher for Capcom RD3 'Oogie's Revenge'
Generates:
1. rom/extracted/default_pc.exe (Flat single-section PE with 1:1 memory mapping)

"""

import os
import sys
import struct

def align(val, alignment):
    return (val + alignment - 1) & ~(alignment - 1)

# Xbox Kernel ordinals with argument counts
# (arg_count * 4 = stack bytes for stdcall 'ret N')
XBOX_KERNEL_ORDINALS = {
    14:  ("AvSetDisplayMode", 6),
    18:  ("DbgPrint", 0),               # cdecl
    71:  ("IoCreateDevice", 7),
    165: ("MmAllocateContiguousMemory", 2),
    166: ("MmAllocateContiguousMemoryEx", 6),
    168: ("MmFreeContiguousMemory", 1),
    170: ("MmMapIoSpace", 3),
    173: ("MmPersistContiguousMemory", 3),
    182: ("NtClose", 1),
    187: ("NtSuspendThread", 2),
    190: ("NtWaitForSingleObject", 3),
    196: ("RtlAnsiStringToUnicodeString", 3),
    197: ("RtlAppendStringToString", 2),
    198: ("RtlAppendUnicodeToString", 2),
    200: ("RtlAssert", 4),
    211: ("RtlCopyString", 2),
    218: ("RtlFreeAnsiString", 1),
    219: ("RtlFreeUnicodeString", 1),
    226: ("RtlInitUnicodeString", 2),
    232: ("RtlIntegerToChar", 4),
    233: ("RtlIntegerToUnicodeString", 3),
    236: ("RtlLowerChar", 1),
    246: ("KeInitializeEvent", 3),
    250: ("KeSetEvent", 3),
    254: ("PsCreateSystemThread", 7),
    255: ("PsCreateSystemThreadEx", 8),
    259: ("RtlEnterCriticalSection", 1),
    260: ("RtlLeaveCriticalSection", 1),
    277: ("RtlInitAnsiString", 2),
    291: ("RtlUnicodeStringToAnsiString", 3),
    294: ("RtlZeroMemory", 2),
    301: ("XboxKrnlVersion", 0),
    320: ("XeImageFileName", 0),        # DATA
    322: ("XboxHardwareInfo", 0),       # DATA
    323: ("XboxCertificate", 0),        # DATA
    324: ("XboxKrnlVersionData", 0),    # DATA
    325: ("LaunchDataPage", 0),         # DATA
}

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

    # Neutralize ALL %fs:[0x20] and %fs:[0x28] Xbox KPCR/ETHREAD accesses
    # Redirect them to safe writable dummy buffer at VA 0x00413d00
    dummy_kpcr_va = 0x00413d00
    kpcr_bytes_6 = b'\xB8' + struct.pack('<I', dummy_kpcr_va) + b'\x90' # mov eax, 0x00413d00; nop (6 bytes)
    kpcr_bytes_7 = b'\xB8' + struct.pack('<I', dummy_kpcr_va) + b'\x90\x90' # 7 bytes

    fs_patterns = [
        (b'\x64\xa1\x20\x00\x00\x00', kpcr_bytes_6, "mov eax, fs:[0x20]"),
        (b'\x64\xa1\x28\x00\x00\x00', kpcr_bytes_6, "mov eax, fs:[0x28]"),
        (b'\x64\x8b\x05\x20\x00\x00\x00', kpcr_bytes_7, "mov eax, fs:[0x20]"),
        (b'\x64\x8b\x05\x28\x00\x00\x00', kpcr_bytes_7, "mov eax, fs:[0x28]")
    ]

    fs_patch_count = 0
    for pat, repl, desc in fs_patterns:
        pos = 0
        while True:
            idx = image_mem.find(pat, pos)
            if idx == -1: break
            image_mem[idx : idx + len(repl)] = repl
            va = base_addr + idx
            print(f"[+] [FS PATCH] Replaced {desc} at VA 0x{va:08x} -> dummy KPCR (0x{dummy_kpcr_va:08x})")
            fs_patch_count += 1
            pos = idx + len(repl)
    print(f"[*] Total %fs KPCR/ETHREAD accesses neutralized: {fs_patch_count}")

    # Patch 9: Direct boot into Capcom RD3 main loop (0x1bf5d3) from Entry Point (0x1bf647)
    # 0x001bf5d3 - (0x001bf647 + 5) = -0x79 = 0xFFFFFF87
    p9_off = 0x001bf647 - base_addr
    image_mem[p9_off : p9_off + 5] = b'\xE9\x87\xFF\xFF\xFF'
    print(f"[+] [PATCH 9] Boot entry point redirected directly to Capcom RD3 main (0x001bf5d3)")

    # 4. Construct Windows PE Headers and .idata Section
    iat_va = 0x00260020
    iat_rva = iat_va - base_addr # 0x00250020
    iat_off = iat_rva

    # Read 125 ordinals from original IAT table
    ords = []
    for i in range(125):
        val = struct.unpack('<I', image_mem[iat_off + i*4 : iat_off + (i+1)*4])[0]
        ords.append(val & 0x7fffffff)
    print(f"[*] Read {len(ords)} Xbox Kernel import ordinals from IAT RVA 0x{iat_rva:08x}")

    # Layout for .idata section
    section_alignment = 0x1000
    file_alignment = 0x1000

    flat_sec_vaddr = 0x1000
    flat_sec_vsize = total_span - flat_sec_vaddr
    flat_sec_rsize = align(flat_sec_vsize, file_alignment)
    flat_sec_raddr = 0x1000

    idata_sec_vaddr = align(flat_sec_vaddr + flat_sec_vsize, section_alignment)
    idata_sec_raddr = align(flat_sec_raddr + flat_sec_rsize, file_alignment)

    # In .idata:
    # 0x00: IMAGE_IMPORT_DESCRIPTOR (20 bytes)
    # 0x14: null terminator descriptor (20 bytes)
    # 0x28: OriginalFirstThunk (INT): 125 * 4 bytes + 4 bytes null
    # after INT: DLL name "xboxkrnl.dll\0"
    num_ords = len(ords)
    int_offset = 40
    int_rva = idata_sec_vaddr + int_offset
    dll_name_offset = int_offset + (num_ords + 1) * 4
    dll_name_rva = idata_sec_vaddr + dll_name_offset

    import_desc = struct.pack(
        '<IIIII',
        int_rva,        # OriginalFirstThunk
        0,              # TimeDateStamp
        0,              # ForwarderChain
        dll_name_rva,   # Name
        iat_rva         # FirstThunk (points to VA 0x00260020)
    )
    null_desc = bytes(20)

    int_entries = bytearray()
    for o in ords:
        # High bit set indicates import by ordinal
        int_entries.extend(struct.pack('<I', 0x80000000 | o))
    int_entries.extend(bytes(4)) # null terminator

    dll_name_bytes = b'xboxkrnl.dll\x00\x00\x00\x00'

    idata_payload = bytearray()
    idata_payload.extend(import_desc)
    idata_payload.extend(null_desc)
    idata_payload.extend(int_entries)
    idata_payload.extend(dll_name_bytes)

    idata_sec_vsize = len(idata_payload)
    idata_sec_rsize = align(idata_sec_vsize, file_alignment)
    idata_payload.extend(b'\x00' * (idata_sec_rsize - idata_sec_vsize))

    total_image_size = idata_sec_vaddr + align(idata_sec_vsize, section_alignment)

    # Assemble PE Headers
    dos_hdr = bytearray(64)
    dos_hdr[0:2] = b'MZ'
    pe_hdr_offset = 0xC00
    struct.pack_into('<I', dos_hdr, 0x3C, pe_hdr_offset)

    pe_sig = b'PE\x00\x00'
    coff_hdr = struct.pack(
        '<HHIIIHH',
        0x014c, 2, 0, 0, 0, 224, 0x0102
    )

    opt_hdr = struct.pack(
        '<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII',
        0x010b, 7, 10,
        flat_sec_rsize, idata_sec_rsize, 0,
        entry_point_va - base_addr, # EntryPoint
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
    # Import Directory (index 1)
    struct.pack_into('<II', data_dirs, 1*8, idata_sec_vaddr, idata_sec_vsize)
    # IAT Directory (index 12)
    struct.pack_into('<II', data_dirs, 12*8, iat_rva, num_ords * 4)

    sec1 = struct.pack(
        '<8sIIIIIIHHI',
        b'.flat\x00\x00\x00',
        flat_sec_vsize, flat_sec_vaddr,
        flat_sec_rsize, flat_sec_raddr,
        0, 0, 0, 0,
        0xE0000060  # RWX Code & Data
    )

    sec2 = struct.pack(
        '<8sIIIIIIHHI',
        b'.idata\x00\x00',
        align(idata_sec_vsize, section_alignment), idata_sec_vaddr,
        idata_sec_rsize, idata_sec_raddr,
        0, 0, 0, 0,
        0xC0000040  # RW Data
    )

    header_block = bytearray(0x1000)
    header_block[0:64] = dos_hdr
    header_block[64:pe_hdr_offset] = xbe[64:pe_hdr_offset]

    off = pe_hdr_offset
    header_block[off : off + 4] = pe_sig
    off += 4
    header_block[off : off + len(coff_hdr)] = coff_hdr
    off += len(coff_hdr)
    header_block[off : off + len(opt_hdr)] = opt_hdr
    off += len(opt_hdr)
    header_block[off : off + len(data_dirs)] = data_dirs
    off += len(data_dirs)
    header_block[off : off + len(sec1)] = sec1
    off += len(sec1)
    header_block[off : off + len(sec2)] = sec2

    image_mem[0:0x1000] = header_block

    # Write patched default_pc.exe
    with open(pe_path, 'wb') as out_f:
        out_f.write(image_mem)
        out_f.write(idata_payload)

    print(f"[+] Successfully generated patched PE: '{pe_path}' ({os.path.getsize(pe_path):,} bytes)")


if __name__ == '__main__':
    xbe = "rom/extracted/default.xbe"
    pe = "rom/extracted/default_pc.exe"
    build_flat_binary(xbe, pe)
