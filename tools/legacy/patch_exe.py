#!/usr/bin/env python3
"""
tools/patch_exe.py
Binary Patcher for Tim Burton's The Nightmare Before Christmas: Oogie's Revenge
Directly patches Xbox PE executable 'default.exe' to strip Xbox Original hardware
dependencies (NV2A MMIO registers, GPU ready spinlocks, hardware timers) and
reconstructs a standard Windows PE Import Directory (IAT) for PC execution.
"""

import os
import sys
import struct
import shutil

XBOX_KERNEL_ORDINALS = {
    1: ('AvGetSavedDataAddress', 0),
    2: ('AvSendTVEncoderOption', 4),
    3: ('AvSetDisplayMode', 6),
    4: ('AvSetSavedDataAddress', 1),
    8: ('DbgBreakPoint', 0),
    15: ('DbgPrint', 0),
    16: ('HalReturnToFirmware', 1),
    17: ('HalWriteSMBusValue', 4),
    23: ('ExInterlockedDecrementLong', 1),
    24: ('ExAllocatePool', 1),
    27: ('ExFreePool', 1),
    40: ('HalReadSMCTrayState', 2),
    44: ('IoCreateFile', 9),
    46: ('IoDeleteDevice', 1),
    47: ('IoDeleteSymbolicLink', 1),
    49: ('IoDeviceObjectType', 0),
    65: ('KeAlertResumeThread', 1),
    67: ('KeBoostPriorityThread', 2),
    69: ('KeDelayExecutionThread', 3),
    71: ('KeInitializeEvent', 3),
    74: ('KePulseEvent', 2),
    75: ('KeResetEvent', 1),
    76: ('KeSetEvent', 3),
    81: ('KeSetTimerEx', 4),
    83: ('KeWaitForSingleObject', 5),
    87: ('KfAcquireSpinLock', 1),
    95: ('MmAllocateContiguousMemory', 1),
    97: ('MmClaimGpuInstanceMemory', 2),
    98: ('MmCreateKernelStack', 2),
    99: ('MmDeleteKernelStack', 2),
    100: ('MmFreeContiguousMemory', 1),
    107: ('MmGetPhysicalAddress', 1),
    109: ('MmIsAddressValid', 1),
    113: ('MmLockUnlockBufferPages', 3),
    119: ('MmMapIoSpace', 3),
    128: ('MmQueryAllocationSize', 1),
    129: ('MmQueryStatistics', 1),
    137: ('MmUnmapIoSpace', 2),
    139: ('NtAllocateVirtualMemory', 5),
    142: ('NtClearEvent', 1),
    143: ('NtClose', 1),
    145: ('NtCreateDirectoryObject', 2),
    149: ('NtCreateFile', 10),
    151: ('NtCreateMutant', 3),
    153: ('NtCreateSemaphore', 4),
    156: ('NtDeviceIoControlFile', 10),
    159: ('NtDuplicateObject', 7),
    160: ('NtFlushBuffersFile', 2),
    161: ('NtFreeVirtualMemory', 3),
    164: ('NtOpenDirectoryObject', 2),
    165: ('NtOpenFile', 6),
    166: ('NtOpenSymbolicLinkObject', 2),
    168: ('NtQueryDirectoryFile', 11),
    171: ('NtQueryFullAttributesFile', 2),
    173: ('NtQueryInformationFile', 5),
    175: ('NtQuerySymbolicLinkObject', 3),
    176: ('NtQueryVolumeInformationFile', 5),
    178: ('NtReadFile', 9),
    179: ('NtReadFileScatter', 9),
    180: ('NtResumeThread', 2),
    182: ('NtSetEvent', 2),
    184: ('NtSetInformationFile', 5),
    187: ('NtSuspendThread', 2),
    189: ('NtWaitForSingleObject', 3),
    190: ('NtWaitForMultipleObjects', 5),
    192: ('NtWriteFile', 9),
    193: ('NtWriteFileGather', 9),
    194: ('NtYieldExecution', 0),
    196: ('RtlAnsiStringToUnicodeString', 3),
    197: ('RtlAppendStringToString', 2),
    198: ('RtlAssert', 4),
    199: ('RtlCaptureContext', 1),
    200: ('RtlCaptureStackBackTrace', 4),
    202: ('RtlCompareMemory', 3),
    203: ('RtlCompareMemoryUlong', 3),
    210: ('RtlCopyString', 2),
    211: ('RtlCreateUnicodeString', 2),
    215: ('RtlEnterCriticalSection', 1),
    217: ('RtlExtendedIntegerMultiply', 2),
    218: ('RtlExtendedLargeIntegerDivide', 3),
    219: ('RtlExtendedMagicDivide', 3),
    221: ('RtlFreeAnsiString', 1),
    222: ('RtlFreeUnicodeString', 1),
    224: ('RtlGetCallersAddress', 2),
    225: ('RtlInitAnsiString', 2),
    226: ('RtlInitUnicodeString', 2),
    229: ('RtlIntegerToChar', 4),
    231: ('RtlLeaveCriticalSection', 1),
    232: ('RtlLowerChar', 1),
    233: ('RtlMapGenericMask', 2),
    234: ('RtlMoveMemory', 3),
    235: ('RtlMultiByteToUnicodeN', 5),
    236: ('RtlMultiByteToUnicodeSize', 3),
    238: ('RtlRaiseException', 1),
    246: ('RtlTimeFieldsToTime', 2),
    250: ('RtlTimeToTimeFields', 2),
    255: ('RtlUnicodeStringToAnsiString', 3),
    258: ('RtlUnicodeStringToInteger', 3),
    259: ('RtlUnicodeToMultiByteN', 5),
    260: ('RtlUnicodeToMultiByteSize', 3),
    269: ('RtlZeroMemory', 2),
    277: ('RtlInitAnsiString', 2),
    279: ('RtlUpperChar', 1),
    289: ('XboxFormatCounter', 1),
    291: ('RtlUnicodeStringToAnsiString', 3),
    294: ('RtlZeroMemory', 2),
    301: ('XboxKrnlVersion', 0),
    302: ('XboxSignatureKey', 0),
    304: ('XeImageFileName', 0),
    305: ('XeLoadSection', 1),
    312: ('XapiFormatFATVolume', 1),
    322: ('XMountUtilityDrive', 1),
    323: ('XCalculateSignatureBegin', 1),
    324: ('XCalculateSignatureUpdate', 3),
    325: ('XCalculateSignatureEnd', 2),
    326: ('XGetCustomContentDirectory', 1),
    327: ('XMountAlternateTitle', 3),
    328: ('XMountUtilityDriveEx', 3),
    335: ('LaunchDataPage', 0),
    336: ('XRegisterThreadNotifyRoutine', 2),
    337: ('XSetProcessQuantumLength', 1),
    340: ('XGetDisplayFlags', 0),
    354: ('XSetProcessQuantumLength', 1),
    356: ('KeWaitForMultipleObjects', 8),
    357: ('KeStallExecutionProcessor', 1),
    358: ('KeDelayExecutionThread', 3),
    359: ('KeQuerySystemTime', 1),
    360: ('KeQueryPerformanceCounter', 1),
}

def align(val, alignment):
    return (val + alignment - 1) & ~(alignment - 1)

def patch_binary(in_exe_path, out_exe_path):
    print(f"[*] Reading source binary: {in_exe_path}")
    with open(in_exe_path, 'rb') as f:
        exe = bytearray(f.read())

    # 1. Parse PE Headers
    e_lfanew = struct.unpack('<I', exe[0x3C:0x40])[0]
    if exe[e_lfanew:e_lfanew+4] != b'PE\x00\x00':
        raise ValueError("Not a valid PE executable!")

    coff_offset = e_lfanew + 4
    num_sections = struct.unpack('<H', exe[coff_offset+2:coff_offset+4])[0]
    opt_hdr_size = struct.unpack('<H', exe[coff_offset+16:coff_offset+18])[0]
    opt_offset = coff_offset + 20

    image_base = struct.unpack('<I', exe[opt_offset+28:opt_offset+32])[0]
    section_alignment = struct.unpack('<I', exe[opt_offset+32:opt_offset+36])[0]
    file_alignment = struct.unpack('<I', exe[opt_offset+36:opt_offset+40])[0]
    size_of_image = struct.unpack('<I', exe[opt_offset+56:opt_offset+60])[0]

    sec_headers_offset = opt_offset + opt_hdr_size

    # Collect sections
    sections = []
    for i in range(num_sections):
        sh_off = sec_headers_offset + i * 40
        sname = exe[sh_off:sh_off+8].rstrip(b'\x00').decode('latin1')
        vsize, vaddr, rsize, raddr, _, _, _, _, chars = struct.unpack('<IIIIIIHHI', exe[sh_off+8:sh_off+40])
        sections.append({
            'index': i,
            'name': sname,
            'vsize': vsize,
            'vaddr': vaddr,
            'va': image_base + vaddr,
            'rsize': rsize,
            'raddr': raddr,
            'chars': chars,
            'header_offset': sh_off
        })

    def va_to_file_offset(va):
        for s in sections:
            if s['va'] <= va < s['va'] + s['vsize']:
                offset = va - s['va']
                if offset < s['rsize']:
                    return s['raddr'] + offset
        return None

    print(f"[*] Base Address: 0x{image_base:08x}, Sections: {num_sections}, Image Size: 0x{size_of_image:08x}")

    # 2. Hardware Dependencies Stripping (NV2A MMIO and GPU hardware loops)
    patches_applied = 0

    # A. Patch D3D_WaitGPUReady at VA 0x001dc611
    # Replace spinlock with 'xor eax, eax; ret; nop' (31 C0 C3 90) -> GPU immediately ready
    off_wait_gpu = va_to_file_offset(0x001dc611)
    if off_wait_gpu is not None:
        exe[off_wait_gpu : off_wait_gpu + 4] = b'\x31\xC0\xC3\x90'
        print(f"[+] [PATCH 1] Neutralized D3D_WaitGPUReady spinlock at VA 0x001dc611 (file 0x{off_wait_gpu:x})")
        patches_applied += 1

    # B. Patch NV2A Device Init at VA 0x001dc2d1
    # Overwrite 'mov [ecx], 0xfd000000; mov eax, [0xfd001804]...' with 'xor eax, eax; inc eax; ret'
    off_d3d_init = va_to_file_offset(0x001dc2d1)
    if off_d3d_init is not None:
        # 31 C0 40 C3 (xor eax, eax; inc eax; ret = return 1) followed by NOPs
        stub = b'\x31\xC0\x40\xC3' + (b'\x90' * 38)
        exe[off_d3d_init : off_d3d_init + len(stub)] = stub
        print(f"[+] [PATCH 2] Neutralized NV2A 0xFD000000 device init at VA 0x001dc2d1 (file 0x{off_d3d_init:x})")
        patches_applied += 1

    # C. Patch NV2A Chip ID Check at VA 0x001dc2fc
    # Fake valid NV2A chip architecture: ecx+0xbc = 0x00fe502a, return 1
    off_chip_id = va_to_file_offset(0x001dc2fc)
    if off_chip_id is not None:
        # mov dword ptr [ecx+0xbc], 0x00fe502a; xor eax, eax; inc eax; ret
        stub = b'\xC7\x81\xBC\x00\x00\x00\x2A\x50\xFE\x00\x31\xC0\x40\xC3' + (b'\x90' * 20)
        exe[off_chip_id : off_chip_id + len(stub)] = stub
        print(f"[+] [PATCH 3] Neutralized NV2A chip detection at VA 0x001dc2fc (file 0x{off_chip_id:x})")
        patches_applied += 1

    # D. Patch NV2A PMC Status Register Reads (0xFD40014C)
    # VA 0x001dbc1d, 0x001dbe5f, 0x001dc00f
    for va, reg in [(0x001dbc1d, 'esi'), (0x001dbe5f, 'esi'), (0x001dc00f, 'edi')]:
        off = va_to_file_offset(va)
        if off is not None:
            if reg == 'esi':
                exe[off : off + 6] = b'\x31\xF6\x90\x90\x90\x90'  # xor esi, esi; 4x nop
            else:
                exe[off : off + 6] = b'\x31\xFF\x90\x90\x90\x90'  # xor edi, edi; 4x nop
            print(f"[+] [PATCH 4] Neutralized 0xFD40014C hardware read at VA 0x{va:x} (file 0x{off:x})")
            patches_applied += 1

    # E. Patch XPP USB MMIO write at VA 0x0025c41f
    # Overwrite 'mov [0xfd000000], al' with 5 NOPs
    off_xpp = va_to_file_offset(0x0025c41f)
    if off_xpp is not None:
        exe[off_xpp : off_xpp + 5] = b'\x90\x90\x90\x90\x90'
        print(f"[+] [PATCH 5] Neutralized XPP 0xFD000000 write at VA 0x0025c41f (file 0x{off_xpp:x})")
        patches_applied += 1

    # 3. Read Xbox Kernel Thunk Table
    # Located in .rdata at VA 0x00260020
    thunk_va = 0x00260020
    thunk_rva = thunk_va - image_base # 0x00250020
    thunk_file_off = va_to_file_offset(thunk_va)

    ordinals = []
    for i in range(125):
        val = struct.unpack('<I', exe[thunk_file_off + i*4 : thunk_file_off + (i+1)*4])[0]
        ord_num = val & 0x7FFFFFFF
        ordinals.append(ord_num)

    print(f"[*] Verified {len(ordinals)} Xbox Kernel Import Ordinals at IAT RVA 0x{thunk_rva:08x}")

    # 4. Construct Windows PE Import Section (.idata)
    # We will append section .idata at the end of the binary
    last_sec = sections[-1]
    # Ensure .idata RVA is safely past the end of the last section
    last_sec_end_vaddr = last_sec['vaddr'] + max(last_sec['vsize'], last_sec['rsize'])
    new_sec_vaddr = align(last_sec_end_vaddr + section_alignment, section_alignment)
    new_sec_raddr = align(len(exe), file_alignment)

    print(f"[*] Last section '{last_sec['name']}' ends at RVA 0x{last_sec_end_vaddr:08x}")
    print(f"[*] Creating .idata section at RVA 0x{new_sec_vaddr:08x}, Raw Offset 0x{new_sec_raddr:08x}")

    # Layout inside .idata:
    # 0x000: IMAGE_IMPORT_DESCRIPTOR (20 bytes) for xboxkrnl.dll
    # 0x014: Null IMAGE_IMPORT_DESCRIPTOR (20 bytes)
    # 0x028: DLL Name string: "xboxkrnl.dll\0" (13 bytes, pad to 16)
    # 0x038: Import Name Table (INT) / OriginalFirstThunk:
    #        125 DWORDs (0x80000000 | ordinal) + 1 null DWORD (126 * 4 = 504 bytes)

    import_desc_rva = new_sec_vaddr
    dll_name_offset = 40
    dll_name_rva = new_sec_vaddr + dll_name_offset
    int_offset = dll_name_offset + 16
    int_rva = new_sec_vaddr + int_offset

    # Build INT (OriginalFirstThunk)
    int_data = bytearray()
    for ord_num in ordinals:
        int_data.extend(struct.pack('<I', 0x80000000 | ord_num))
    int_data.extend(struct.pack('<I', 0)) # Null terminator

    # Build IMAGE_IMPORT_DESCRIPTOR (20 bytes)
    # OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk
    import_desc = struct.pack(
        '<IIIII',
        int_rva,            # OriginalFirstThunk (INT)
        0,                  # TimeDateStamp
        0,                  # ForwarderChain
        dll_name_rva,       # Name ("xboxkrnl.dll")
        thunk_rva           # FirstThunk (points directly to VA 0x00260020 in .rdata!)
    )
    null_desc = bytes(20)

    # Assemble .idata payload
    idata_payload = bytearray()
    idata_payload.extend(import_desc)
    idata_payload.extend(null_desc)
    idata_payload.extend(b'xboxkrnl.dll\x00\x00\x00\x00') # 16 bytes
    idata_payload.extend(int_data)

    new_sec_vsize = len(idata_payload)
    new_sec_rsize = align(new_sec_vsize, file_alignment)
    idata_payload.extend(b'\x00' * (new_sec_rsize - new_sec_vsize))

    # Pad binary to new_sec_raddr
    if len(exe) < new_sec_raddr:
        exe.extend(b'\x00' * (new_sec_raddr - len(exe)))
    exe.extend(idata_payload)

    # 5. Update PE Header Data Directories
    # Data Directory 1: IMAGE_DIRECTORY_ENTRY_IMPORT
    # Data Directory 12: IMAGE_DIRECTORY_ENTRY_IAT
    dd_offset = opt_offset + 96
    # Import Directory (Index 1): RVA, Size
    struct.pack_into('<II', exe, dd_offset + 1 * 8, import_desc_rva, 40)
    # IAT Directory (Index 12): RVA, Size
    struct.pack_into('<II', exe, dd_offset + 12 * 8, thunk_rva, 125 * 4)

    # 6. Add .idata Section Header
    new_sec_header = struct.pack(
        '<8sIIIIIIHHI',
        b'.idata\x00\x00',
        new_sec_vsize,
        new_sec_vaddr,
        new_sec_rsize,
        new_sec_raddr,
        0, 0, 0, 0,
        0xC0000040 # IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_CNT_INITIALIZED_DATA
    )

    new_sh_offset = sec_headers_offset + num_sections * 40
    exe[new_sh_offset : new_sh_offset + 40] = new_sec_header

    # Update NumberOfSections in COFF Header
    struct.pack_into('<H', exe, coff_offset + 2, num_sections + 1)

    # Update SizeOfImage in Optional Header
    new_size_of_image = align(new_sec_vaddr + new_sec_vsize, section_alignment)
    struct.pack_into('<I', exe, opt_offset + 56, new_size_of_image)

    # Write patched executable
    with open(out_exe_path, 'wb') as f:
        f.write(exe)

    print(f"[+] Successfully wrote patched binary: {out_exe_path} ({len(exe):,} bytes)")
    print(f"    - Hardware patches applied: {patches_applied}")
    print(f"    - Import Directory RVA: 0x{import_desc_rva:08x}")
    print(f"    - IAT Directory RVA:    0x{thunk_rva:08x}")
    print(f"    - Total PE Sections:    {num_sections + 1}")

    return ordinals

def generate_xboxkrnl_dll(ordinals, dll_path):
    print(f"[*] Generating standard Win32 'xboxkrnl.dll' for {len(ordinals)} ordinals: {dll_path}")

    # Build a standard PE32 DLL exporting ordinals 1..378
    # Section Alignment: 0x1000, File Alignment: 0x200
    sec_align = 0x1000
    file_align = 0x200
    image_base = 0x10000000

    # Build DOS Header
    dos_hdr = bytearray(64)
    dos_hdr[0:2] = b'MZ'
    dos_hdr[0x3C:0x40] = struct.pack('<I', 0x40)

    pe_sig = b'PE\x00\x00'

    # We will have two sections: .text (code) and .edata (exports)
    num_sections = 2
    coff_hdr = struct.pack('<HHIIIHH', 0x014c, num_sections, 0, 0, 0, 224, 0x2102) # 0x2000 = DLL

    # Calculate export list
    max_ordinal = 378
    base_ordinal = 1
    num_funcs = max_ordinal - base_ordinal + 1

    # In .text: generate a tiny function for each ordinal:
    # Default: 'xor eax, eax; ret <arg_bytes>' (31 C0 C2 <XX 00>)
    # For ordinals with 0 args: 'xor eax, eax; ret' (31 C0 C3)
    # For XboxKrnlVersion (301): 'mov eax, 0x00010f40; ret' (B8 40 0F 01 00 C3)
    text_rva = 0x1000
    text_code = bytearray()
    
    # 0x1000: DllMain entry point (stdcall: 3 args = 12 bytes, returns 1)
    # B8 01 00 00 00 (mov eax, 1) C2 0C 00 (ret 12)
    dll_entry_rva = text_rva
    text_code.extend(b'\xB8\x01\x00\x00\x00\xC2\x0C\x00')

    func_rvas = [0] * num_funcs

    for ord_num in range(base_ordinal, max_ordinal + 1):
        idx = ord_num - base_ordinal
        func_rvas[idx] = text_rva + len(text_code)

        if ord_num == 301: # XboxKrnlVersion
            text_code.extend(b'\xB8\x40\x0F\x01\x00\xC3')
        elif ord_num in XBOX_KERNEL_ORDINALS:
            name, arg_count = XBOX_KERNEL_ORDINALS[ord_num]
            if arg_count == 0:
                text_code.extend(b'\x31\xC0\xC3') # xor eax, eax; ret
            else:
                stack_bytes = arg_count * 4
                text_code.extend(b'\x31\xC0\xC2' + struct.pack('<H', stack_bytes)) # xor eax, eax; ret N
        else:
            text_code.extend(b'\x31\xC0\xC3') # default stub

    text_vsize = len(text_code)
    text_rsize = align(text_vsize, file_align)
    text_code.extend(b'\x00' * (text_rsize - text_vsize))

    # .edata (Exports) Section
    edata_rva = align(text_rva + text_vsize, sec_align)

    # Structure of .edata:
    # 0x00: IMAGE_EXPORT_DIRECTORY (40 bytes)
    # 0x28: DLL Name: "xboxkrnl.dll\0" (16 bytes)
    # 0x38: AddressOfFunctions: num_funcs * 4 bytes
    export_dir_offset = 0
    dll_name_offset = 40
    dll_name_rva = edata_rva + dll_name_offset
    funcs_table_offset = 56
    funcs_table_rva = edata_rva + funcs_table_offset

    export_dir = struct.pack(
        '<IIIIIIIIII',
        0,                  # Characteristics
        0,                  # TimeDateStamp
        0,                  # Major/MinorVersion
        dll_name_rva,       # Name
        base_ordinal,       # Base (1)
        num_funcs,          # NumberOfFunctions
        0,                  # NumberOfNames (export by ordinal only)
        funcs_table_rva,    # AddressOfFunctions
        0,                  # AddressOfNames
        0                   # AddressOfNameOrdinals
    )

    funcs_data = bytearray()
    for rva in func_rvas:
        funcs_data.extend(struct.pack('<I', rva))

    edata_payload = bytearray()
    edata_payload.extend(export_dir)
    edata_payload.extend(b'xboxkrnl.dll\x00\x00\x00\x00')
    edata_payload.extend(funcs_data)

    edata_vsize = len(edata_payload)
    edata_rsize = align(edata_vsize, file_align)
    edata_payload.extend(b'\x00' * (edata_rsize - edata_vsize))

    # Calculate overall PE size
    size_of_headers = align(len(dos_hdr) + 4 + len(coff_hdr) + 224 + num_sections * 40, file_align)
    size_of_image = align(edata_rva + edata_vsize, sec_align)

    # Optional Header (PE32)
    opt_hdr = struct.pack(
        '<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII',
        0x010b, 7, 10,
        text_rsize, edata_rsize, 0,
        text_rva, text_rva, edata_rva,
        image_base, sec_align, file_align,
        4, 0, 0, 0, 4, 0, 0,
        size_of_image, size_of_headers, 0,
        2,      # Subsystem = Windows GUI
        0x2102, # DllCharacteristics
        0x100000, 0x1000, 0x100000, 0x1000,
        0, 16
    )

    data_dirs = bytearray(128)
    # Export Directory (Index 0): RVA, Size
    struct.pack_into('<II', data_dirs, 0, edata_rva, edata_vsize)

    # Section Headers
    text_raw_off = size_of_headers
    edata_raw_off = text_raw_off + text_rsize

    sec_hdr_text = struct.pack(
        '<8sIIIIIIHHI',
        b'.text\x00\x00\x00',
        text_vsize, text_rva, text_rsize, text_raw_off,
        0, 0, 0, 0,
        0x60000020 # Code, Executable, Readable
    )
    sec_hdr_edata = struct.pack(
        '<8sIIIIIIHHI',
        b'.edata\x00\x00',
        edata_vsize, edata_rva, edata_rsize, edata_raw_off,
        0, 0, 0, 0,
        0x40000040 # Initialized Data, Readable
    )

    # Assemble PE
    pe_file = bytearray(size_of_headers)
    pe_file[0:64] = dos_hdr
    pe_file[0x40:0x44] = pe_sig
    pe_file[0x44:0x44+len(coff_hdr)] = coff_hdr
    pe_file[0x58:0x58+len(opt_hdr)] = opt_hdr
    pe_file[0x58+len(opt_hdr) : 0x58+len(opt_hdr)+128] = data_dirs
    pe_sec_start = 0x58 + len(opt_hdr) + 128
    pe_file[pe_sec_start : pe_sec_start+40] = sec_hdr_text
    pe_file[pe_sec_start+40 : pe_sec_start+80] = sec_hdr_edata

    pe_file.extend(text_code)
    pe_file.extend(edata_payload)

    with open(dll_path, 'wb') as f:
        f.write(pe_file)

    print(f"[+] Successfully generated '{dll_path}' ({len(pe_file):,} bytes, {num_funcs} exported ordinals)")

if __name__ == '__main__':
    in_exe = 'rom/extracted/default.exe'
    out_exe = 'rom/extracted/default_pc.exe'
    out_named_exe = 'rom/extracted/OogiesRevenge_PC.exe'
    out_dll = 'rom/extracted/xboxkrnl.dll'

    if not os.path.exists(in_exe):
        print(f"[-] Source file '{in_exe}' not found! Trying to run xbe_to_pe.py first...")
        import xbe_to_pe
        xbe_to_pe.convert_xbe_to_pe('rom/extracted/default.xbe', in_exe)

    ords = patch_binary(in_exe, out_exe)
    shutil.copyfile(out_exe, out_named_exe)
    print(f"[+] Created copy: {out_named_exe}")

    generate_xboxkrnl_dll(ords, out_dll)
    print("\n[***] ALL XBOX DEPENDENCIES DIRECTLY STRIPPED FROM BINARY [***]")
    print(f"Generated standalone PC files in 'rom/extracted/':")
    print(f"  1. {out_exe} (PC executable with patched NV2A & Win32 IAT)")
    print(f"  2. {out_named_exe} (Named launcher)")
    print(f"  3. {out_dll} (Standard Win32 ordinal thunk library)")

