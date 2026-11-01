"""
XAPI -> Windows port tables for default.xbe (XDK 5849), used by build_flat_pe.py.

The Capcom game code never calls the Xbox kernel; only the statically linked
XAPI / LIBC (and D3D, DSound, XACT, XPP) do. XAPI exports are a Win32 subset
with identical signatures, so each one is patched into a jump to the same
function of the real kernel32.dll. No Xbox kernel is reimplemented.

Everything this module adds lives in one extra PE section (.port):
import table, a few X* replacements, and the start-up routine.
"""

import struct

import coff_link

# ------------------------------------------------------------------ tables --

# XAPI entry point -> Windows function with the same signature.
XAPI_TO_WIN32 = {
    0x1bce3d: 'CloseHandle',
    0x1bce5b: 'DuplicateHandle',
    0x1bce81: 'GetLastError',
    0x1bcea9: 'SetLastError',
    0x1bceee: 'GetOverlappedResult',
    0x1bcf53: 'ReadFile',
    0x1bd040: 'WriteFile',
    0x1bd116: 'SetEndOfFile',
    0x1bd19a: 'SetFilePointer',
    0x1bd294: 'SetFilePointerEx',
    0x1bd35c: 'FlushFileBuffers',
    0x1bd383: 'GetFileSizeEx',
    0x1bd3c1: 'ReadFileEx',
    0x1bd412: 'GetFileSize',
    0x1bd44d: 'CreateFileA',
    0x1bd5d8: 'CreateEventA',
    0x1bd639: 'SetEvent',
    0x1bd659: 'CreateSemaphoreA',
    0x1bd6b4: 'ReleaseSemaphore',
    0x1bd6da: 'CreateMutexA',
    0x1bd732: 'ReleaseMutex',
    0x1bd752: 'WaitForSingleObjectEx',
    0x1bd796: 'WaitForMultipleObjectsEx',
    0x1bd816: 'SleepEx',
    0x1bd868: 'CreateWaitableTimerA',
    0x1bd8c6: 'SetWaitableTimer',
    0x1bd90c: 'WaitForSingleObject',
    0x1bd91e: 'WaitForMultipleObjects',
    0x1bd938: 'Sleep',
    0x1bd97e: 'SetThreadPriority',
    0x1bd9d0: 'SuspendThread',
    0x1bd9f6: 'ResumeThread',
    0x1bda1c: 'RaiseException',
    0x1bda72: 'SwitchToThread',
    0x1bda85: 'ExitThread',
    0x1bdbdc: 'CreateThread',
    0x1bdc4a: 'GetCurrentThreadId',
    0x1bdccd: 'QueryPerformanceCounter',
    0x1bdcde: 'QueryPerformanceFrequency',
    0x1bdffe: 'GetTickCount',
    0x1be4a2: 'GetLocalTime',
    0x1be677: 'MultiByteToWideChar',
    0x1be732: 'GetDiskFreeSpaceExA',
    0x1bf4b8: 'GetFileAttributesA',
    0x1bf503: 'DeleteFileA',
    # XAPI heap (Rtl*Heap) as called by LIBC
    0x1c0e80: 'GetProcessHeap',
    0x1c2450: 'HeapAlloc',
    0x1c2bcb: 'HeapFree',
    0x1c2dbf: 'HeapReAlloc',
    0x1c1aae: 'HeapSize',
}

# XAPI functions with no Win32 twin: entry -> stub name (see build_port_section).
# XPhysicalAlloc / XMemAlloc live in src/PC/memory.c (tools/pc_render.py).
XAPI_TO_STUB = {
    0x1bdcf2: 'XGetLanguage',
    0x1bde89: 'XGetVideoFlags',
    0x1bded2: 'XGetAVPack',
    0x1bdd1f: 'XLaunchNewImageA',
    0x1bdaa9: 'XRegisterThreadNotifyRoutine',
    0x1bdaf2: 'XSetProcessQuantumLength',
    0x1c0dc8: 'XAutoPowerDownResetTimer',
}

# Kernel thunks still referenced by code that stays (LIBC, and the Xbox media
# libraries until they are replaced): ordinal name -> (dll, function).
KERNEL_TO_WIN32 = {
    'RtlEnterCriticalSection': ('ntdll.dll', 'RtlEnterCriticalSection'),
    'RtlLeaveCriticalSection': ('ntdll.dll', 'RtlLeaveCriticalSection'),
    'RtlInitializeCriticalSection': ('ntdll.dll', 'RtlInitializeCriticalSection'),
    'RtlUnwind': ('ntdll.dll', 'RtlUnwind'),
    'RtlRaiseException': ('ntdll.dll', 'RtlRaiseException'),
    'RtlNtStatusToDosError': ('ntdll.dll', 'RtlNtStatusToDosError'),
    'RtlInitAnsiString': ('ntdll.dll', 'RtlInitAnsiString'),
    'RtlEqualString': ('ntdll.dll', 'RtlEqualString'),
    'RtlCompareMemoryUlong': ('ntdll.dll', 'RtlCompareMemoryUlong'),
    'DbgPrint': ('ntdll.dll', 'DbgPrint'),
    'KeQuerySystemTime': ('kernel32.dll', 'GetSystemTimeAsFileTime'),
}

# XAPI process heap handle (XapiInitProcess is skipped, the entry sets it).
XAPI_PROCESS_HEAP = 0x413bd4

# C runtime start-up as done by the XAPI main thread routine (0x1bf5d3).
RTINIT, CINIT, GAME_MAIN = 0x1c0a91, 0x1c0a39, 0x1aef10

# Path builder 0x1a88f0 turns "host0:data/x" into "d:\data\x": write ".\"
# instead of "d:" (Windows folds the doubled backslash).
PATH_PATCHES = [
    (0x1a8910, bytes.fromhex('c60064'), bytes.fromhex('c6002e')),  # 'd' -> '.'
    (0x1a8914, bytes.fromhex('c6003a'), bytes.fromhex('c6005c')),  # ':' -> '\'
]
# Literal drive paths in .rdata -> relative to the game folder.
DRIVE_PREFIXES = [b'd:\\', b'D:\\', b't:\\', b'T:\\', b'u:\\']

# fs: (Xbox KPCR) accesses, all in XAPI / LIBC / media libraries.
#  fs:[0x04] StackBase + idx*4 = TLS block   -> Windows fs:[0x2C] (same scheme)
#  fs:[0x24] Irql                            -> always PASSIVE_LEVEL (0)
#  fs:[0x20] Prcb, fs:[0x28] CurrentThread   -> static block in .port
#  fs:[0x58] Prcb.DpcRoutineActive           -> 0
FS_PATCHES = [
    (b'\x64\x8b\x0d\x04\x00\x00\x00', b'\x64\x8b\x0d\x2c\x00\x00\x00', 5),
    (b'\x64\x8b\x3d\x04\x00\x00\x00', b'\x64\x8b\x3d\x2c\x00\x00\x00', 1),
    (b'\x64\x0f\xb6\x05\x24\x00\x00\x00', b'\x31\xc0' + b'\x90' * 6, 12),
    (b'\x64\xa1\x58\x00\x00\x00', b'\x31\xc0' + b'\x90' * 4, 2),
]
KPCR_PATTERNS = [b'\x64\xa1\x20\x00\x00\x00', b'\x64\xa1\x28\x00\x00\x00']  # -> mov eax, KPCR_BLOCK

TLS_DIRECTORY_VA = 0x0027c600
KERNEL_THUNKS_VA = 0x00260020

# xboxkrnl.exe exports imported by default.xbe: ordinal -> name (@N = stdcall
# argument bytes, no @ = data export).
KERNEL_ORDINAL_NAMES = {
    1: 'AvGetSavedDataAddress@0',
    2: 'AvSendTVEncoderOption@16',
    3: 'AvSetDisplayMode@24',
    4: 'AvSetSavedDataAddress@4',
    8: 'DbgPrint',
    15: 'ExAllocatePoolWithTag@8',
    16: 'ExEventObjectType',
    17: 'ExFreePool@4',
    23: 'ExQueryPoolBlockSize@4',
    24: 'ExQueryNonVolatileSetting@20',
    40: 'HalDiskCachePartitionCount',
    44: 'HalGetInterruptVector@8',
    46: 'HalReadWritePCISpace@24',
    47: 'HalRegisterShutdownNotification@8',
    49: 'HalReturnToFirmware@4',
    65: 'IoCreateDevice@24',
    67: 'IoCreateSymbolicLink@8',
    69: 'IoDeleteSymbolicLink@4',
    71: 'IoFileObjectType',
    74: 'IoInvalidDeviceRequest@8',
    81: 'IoStartNextPacket@4',
    83: 'IoStartPacket@12',
    87: '@IofCompleteRequest@8',
    95: 'KeBugCheck@4',
    97: 'KeCancelTimer@4',
    98: 'KeConnectInterrupt@4',
    99: 'KeDelayExecutionThread@12',
    100: 'KeDisconnectInterrupt@4',
    107: 'KeInitializeDpc@12',
    109: 'KeInitializeInterrupt@28',
    113: 'KeInitializeTimerEx@8',
    119: 'KeInsertQueueDpc@12',
    128: 'KeQuerySystemTime@4',
    129: 'KeRaiseIrqlToDpcLevel@0',
    137: 'KeRemoveQueueDpc@4',
    139: 'KeRestoreFloatingPointState@4',
    142: 'KeSaveFloatingPointState@4',
    143: 'KeSetBasePriorityThread@8',
    145: 'KeSetEvent@12',
    149: 'KeSetTimer@16',
    151: 'KeStallExecutionProcessor@4',
    153: 'KeSynchronizeExecution@12',
    156: 'KeTickCount',
    159: 'KeWaitForSingleObject@20',
    160: '@KfRaiseIrql@4',
    161: '@KfLowerIrql@4',
    164: 'LaunchDataPage',
    165: 'MmAllocateContiguousMemory@4',
    166: 'MmAllocateContiguousMemoryEx@20',
    168: 'MmClaimGpuInstanceMemory@8',
    171: 'MmFreeContiguousMemory@4',
    173: 'MmGetPhysicalAddress@4',
    175: 'MmLockUnlockBufferPages@12',
    176: 'MmLockUnlockPhysicalPage@8',
    178: 'MmPersistContiguousMemory@12',
    179: 'MmQueryAddressProtect@4',
    180: 'MmQueryAllocationSize@4',
    182: 'MmSetAddressProtect@12',
    184: 'NtAllocateVirtualMemory@20',
    187: 'NtClose@4',
    189: 'NtCreateEvent@16',
    190: 'NtCreateFile@36',
    192: 'NtCreateMutant@12',
    193: 'NtCreateSemaphore@16',
    194: 'NtCreateTimer@12',
    196: 'NtDeviceIoControlFile@40',
    197: 'NtDuplicateObject@12',
    198: 'NtFlushBuffersFile@8',
    199: 'NtFreeVirtualMemory@12',
    200: 'NtFsControlFile@40',
    202: 'NtOpenFile@24',
    203: 'NtOpenSymbolicLinkObject@8',
    210: 'NtQueryFullAttributesFile@8',
    211: 'NtQueryInformationFile@20',
    215: 'NtQuerySymbolicLinkObject@12',
    217: 'NtQueryVirtualMemory@8',
    218: 'NtQueryVolumeInformationFile@20',
    219: 'NtReadFile@32',
    221: 'NtReleaseMutant@8',
    222: 'NtReleaseSemaphore@12',
    224: 'NtResumeThread@8',
    225: 'NtSetEvent@8',
    226: 'NtSetInformationFile@20',
    229: 'NtSetTimerEx@32',
    231: 'NtSuspendThread@8',
    232: 'NtUserIoApcDispatcher@12',
    233: 'NtWaitForSingleObject@12',
    234: 'NtWaitForSingleObjectEx@16',
    235: 'NtWaitForMultipleObjectsEx@24',
    236: 'NtWriteFile@32',
    238: 'NtYieldExecution@0',
    246: 'ObReferenceObjectByHandle@12',
    250: '@ObfDereferenceObject@4',
    255: 'PsCreateSystemThreadEx@40',
    258: 'PsTerminateSystemThread@4',
    259: 'PsThreadObjectType',
    260: 'RtlAnsiStringToUnicodeString@12',
    269: 'RtlCompareMemoryUlong@12',
    277: 'RtlEnterCriticalSection@4',
    279: 'RtlEqualString@12',
    289: 'RtlInitAnsiString@8',
    291: 'RtlInitializeCriticalSection@4',
    294: 'RtlLeaveCriticalSection@4',
    301: 'RtlNtStatusToDosError@4',
    302: 'RtlRaiseException@4',
    304: 'RtlTimeFieldsToTime@8',
    305: 'RtlTimeToTimeFields@8',
    312: 'RtlUnwind@16',
    322: 'XboxHardwareInfo',
    323: 'XboxHDKey',
    324: 'XboxKrnlVersion',
    325: 'XboxSignatureKey',
    326: 'XeImageFileName',
    327: 'XeLoadSection@4',
    328: 'XeUnloadSection@4',
    335: 'XcSHAInit@4',
    336: 'XcSHAUpdate@12',
    337: 'XcSHAFinal@8',
    340: 'XcHMAC@28',
    354: 'XboxAlternateSignatureKeys',
    356: 'HalBootSMCVideoMode',
    357: 'IdexChannelObject',
    358: 'HalIsResetOrShutdownPending@0',
    359: 'IoMarkIrpMustComplete@4',
    360: 'HalInitiateShutdown@0',
}

# ------------------------------------------------------------- assembler --

class Asm:
    """Tiny x86 emitter: just the forms the stubs need."""

    def __init__(self, va):
        self.va = va
        self.b = bytearray()

    def here(self):
        return self.va + len(self.b)

    def raw(self, s):
        self.b += s

    def push(self, imm):
        self.b += (b'\x6a' + struct.pack('<b', imm)) if -128 <= imm < 128 else (b'\x68' + struct.pack('<I', imm & 0xFFFFFFFF))

    def push_arg(self, disp):          # push dword [esp+disp]
        self.b += b'\xff\x74\x24' + bytes([disp])

    def push_eax(self):
        self.b += b'\x50'

    def call_mem(self, addr):          # call dword [addr]
        self.b += b'\xff\x15' + struct.pack('<I', addr)

    def jmp_mem(self, addr):           # jmp dword [addr]
        self.b += b'\xff\x25' + struct.pack('<I', addr)

    def call(self, target):
        self.b += b'\xe8' + struct.pack('<i', target - (self.here() + 5))

    def mov_eax(self, imm):
        self.b += b'\xb8' + struct.pack('<I', imm & 0xFFFFFFFF)

    def mov_mem_eax(self, addr):
        self.b += b'\xa3' + struct.pack('<I', addr)

    def xor_eax(self):
        self.b += b'\x31\xc0'

    def add_esp(self, n):
        self.b += b'\x83\xc4' + bytes([n])

    def ret(self, n=0):
        self.b += (b'\xc2' + struct.pack('<H', n)) if n else b'\xc3'

    def int3(self):
        self.b += b'\xcc'

    def align(self, n=16):
        while len(self.b) % n:
            self.b += b'\xcc'


def jmp_rel(src, dst):
    return b'\xe9' + struct.pack('<i', dst - (src + 5))


def jmp_mem(addr):
    return b'\xff\x25' + struct.pack('<I', addr)


# --------------------------------------------------------- .port section --

def build_port_section(image_mem, base, sec_rva, kernel_names, renderer=None):
    """Fill image_mem patches and return (section bytes, info dict).

    kernel_names: [(thunk_va, ordinal, name)] read from the XBE thunk table.
    renderer: (COFF object, {xbox va: (symbol, desc)}, dll_of) for the PC renderer.
    """
    imports = {}  # dll -> [func]

    def need(dll, fn):
        imports.setdefault(dll, [])
        if fn not in imports[dll]:
            imports[dll].append(fn)

    for fn in XAPI_TO_WIN32.values():
        need('kernel32.dll', fn)
    for fn in ('VirtualAlloc', 'VirtualFree', 'GetProcessHeap', 'HeapAlloc', 'HeapFree', 'ExitProcess',
               'OutputDebugStringA'):
        need('kernel32.dll', fn)
    need('ntdll.dll', 'sprintf')
    if renderer:
        obj, table, dll_of = renderer
        for fn in coff_link.imports_of(obj):
            need(dll_of(fn), fn)
    for _, _, name in kernel_names:
        if base_name(name) in KERNEL_TO_WIN32:
            need(*KERNEL_TO_WIN32[base_name(name)])

    # -- import directory ------------------------------------------------
    va0 = base + sec_rva
    sec = bytearray()
    dlls = sorted(imports)
    desc_off = 0
    sec += bytes((len(dlls) + 1) * 20)
    iat = {}  # func -> VA of its IAT slot
    layout = []
    for dll in dlls:
        n = len(imports[dll])
        ilt = len(sec); sec += bytes((n + 1) * 4)
        iat_off = len(sec); sec += bytes((n + 1) * 4)
        layout.append((dll, ilt, iat_off))
        for k, fn in enumerate(imports[dll]):
            iat[fn] = va0 + iat_off + k * 4
    iat_lo = min(o for _, _, o in layout)
    iat_hi = max(o + (len(imports[d]) + 1) * 4 for d, _, o in layout)
    for di, (dll, ilt, iat_off) in enumerate(layout):
        for k, fn in enumerate(imports[dll]):
            if len(sec) % 2:
                sec.append(0)
            hn = sec_rva + len(sec)
            sec += b'\0\0' + fn.encode() + b'\0'
            struct.pack_into('<I', sec, ilt + k * 4, hn)
            struct.pack_into('<I', sec, iat_off + k * 4, hn)
        name_rva = sec_rva + len(sec)
        sec += dll.encode() + b'\0'
        struct.pack_into('<IIIII', sec, desc_off + di * 20, sec_rva + ilt, 0, 0, name_rva, sec_rva + iat_off)
    import_dir = (sec_rva + desc_off, (len(dlls) + 1) * 20)
    iat_dir = (sec_rva + iat_lo, iat_hi - iat_lo)
    while len(sec) % 16:
        sec.append(0)

    # -- data: KPCR block, zeroed blocks for kernel data exports ----------
    kpcr = va0 + len(sec)
    blk = bytearray(0x1000)
    struct.pack_into('<I', blk, 0x28, 1)  # CurrentThread->TlsData != 0: LIBC uses the TLS slot
    sec += blk
    kdata = va0 + len(sec)
    sec += bytes(0x400)
    trap_fmt = va0 + len(sec)
    sec += b'[port] unported xboxkrnl ordinal %u called from %08x\n\0'
    while len(sec) % 16:
        sec.append(0)
    trap_buf = va0 + len(sec)
    sec += bytes(0x80)

    # -- code ---------------------------------------------------------------
    a = Asm(va0 + len(sec))
    stubs = {}

    def stub(name):
        a.align()
        stubs[name] = a.here()

    stub('XGetLanguage')          # English
    a.mov_eax(1); a.ret()
    stub('XGetVideoFlags')
    a.xor_eax(); a.ret()
    stub('XGetAVPack')
    a.xor_eax(); a.ret()
    stub('XLaunchNewImageA')      # "return to dashboard" = quit
    a.push(0); a.call_mem(iat['ExitProcess'])
    stub('XRegisterThreadNotifyRoutine')
    a.ret(8)
    stub('XSetProcessQuantumLength')
    a.ret(4)
    stub('XAutoPowerDownResetTimer')
    a.ret()

    # kernel thunk targets
    tramp = {}
    for fn in sorted({v for v in KERNEL_TO_WIN32.values()}):
        if fn[1] in iat:
            a.align(8)
            tramp[fn] = a.here()
            a.jmp_mem(iat[fn[1]])
    # unported kernel call: report ordinal + caller, then exit with the ordinal
    a.align()
    trap_common = a.here()             # [esp] = ordinal, [esp+4] = caller
    a.push_arg(4); a.push_arg(4); a.push(trap_fmt); a.push(trap_buf)
    a.call_mem(iat['sprintf']); a.add_esp(16)
    a.push(trap_buf); a.call_mem(iat['OutputDebugStringA'])
    a.call_mem(iat['ExitProcess'])     # ordinal still on the stack = exit code
    trap = {}
    for tva, ordinal, name in kernel_names:
        if base_name(name) in KERNEL_TO_WIN32 or '@' not in name:
            continue
        a.align(8)
        trap[ordinal] = a.here()
        a.push(ordinal); a.raw(jmp_rel(a.here(), trap_common))

    # entry point
    a.align()
    entry = a.here()
    a.call_mem(iat['GetProcessHeap']); a.mov_mem_eax(XAPI_PROCESS_HEAP)
    a.call(RTINIT)
    a.call(CINIT)
    a.push(0); a.push(0); a.push(0); a.call(GAME_MAIN); a.add_esp(12)
    a.push_eax(); a.call_mem(iat['ExitProcess'])
    sec += a.b

    # PC renderer (src/PC), linked after the stubs
    rsyms = {}
    if renderer:
        while len(sec) % 16:
            sec.append(0xCC)
        blob, rsyms = coff_link.link(obj, va0 + len(sec), iat)
        sec += blob

    # -- patches on the XBE image -----------------------------------------
    def put(va, data, expect=None):
        off = va - base
        if expect is not None and bytes(image_mem[off:off + len(expect)]) != expect:
            raise SystemExit(f'[!] 0x{va:08x}: unexpected bytes {image_mem[off:off + len(expect)].hex()}')
        image_mem[off:off + len(data)] = data

    n_bound = n_trap = n_data = 0
    for tva, ordinal, name in kernel_names:
        if base_name(name) in KERNEL_TO_WIN32:
            target = tramp[KERNEL_TO_WIN32[base_name(name)]]; n_bound += 1
        elif '@' in name:
            target = trap[ordinal]; n_trap += 1
        else:  # data export: zeroed block
            target = kdata; n_data += 1
        struct.pack_into('<I', image_mem, tva - base, target)

    for va, old, new in PATH_PATCHES:
        put(va, new, old)
    n_paths = 0
    rdata_lo, rdata_hi = 0x260020 - base, 0x280a74 - base
    for pre in DRIVE_PREFIXES:
        pos = image_mem.find(pre, rdata_lo, rdata_hi)
        while pos != -1:
            image_mem[pos:pos + 3] = b'.\\\\'
            n_paths += 1
            pos = image_mem.find(pre, pos + 3, rdata_hi)

    for pat, rep, expected in FS_PATCHES:
        hits = find_all(image_mem, pat)
        if len(hits) != expected:
            raise SystemExit(f'[!] fs patch {pat.hex()}: expected {expected} sites, found {len(hits)}')
        for h in hits:
            image_mem[h:h + len(rep)] = rep
    n_cs = convert_static_critical_sections(image_mem, base)
    n_kpcr = 0
    for pat in KPCR_PATTERNS:
        for h in find_all(image_mem, pat):
            image_mem[h:h + 6] = b'\xb8' + struct.pack('<I', kpcr) + b'\x90'
            n_kpcr += 1

    # after the fs: patches, which also match inside some replaced entries
    for va, fn in XAPI_TO_WIN32.items():
        put(va, jmp_mem(iat[fn]))
    for va, name in XAPI_TO_STUB.items():
        put(va, jmp_rel(va, stubs[name]))
    if renderer:
        for va, (sym, _) in table.items():
            if sym not in rsyms:
                raise SystemExit(f'[!] renderer symbol missing: {sym}')
            put(va, jmp_rel(va, rsyms[sym]))

    info = dict(import_dir=import_dir, iat_dir=iat_dir, entry_rva=entry - base,
                tls_dir=(TLS_DIRECTORY_VA - base, 0x18),
                stats=dict(xapi=len(XAPI_TO_WIN32), stubs=len(XAPI_TO_STUB), bound=n_bound,
                           traps=n_trap, data=n_data, paths=n_paths, kpcr=n_kpcr, cs=n_cs,
                           d3d=len(table) if renderer else 0))
    return bytes(sec), info


def convert_static_critical_sections(image_mem, base):
    """Rewrite statically initialised Xbox RTL_CRITICAL_SECTIONs to the Windows layout.

    Xbox: {KEVENT header (Type=1, Size=4, SignalState, WaitList self-links), LockCount=-1,
    RecursionCount, OwningThread}, 0x1C bytes. Windows reads LockCount at +4 (here
    SignalState = 0, i.e. "held") and deadlocks. Windows layout (0x18 bytes):
    DebugInfo, LockCount=-1, RecursionCount, OwningThread, LockSemaphore, SpinCount.
    """
    n = 0
    pos = image_mem.find(b'\x01\x00\x04\x00\x00\x00\x00\x00')
    while pos != -1:
        va = base + pos
        flink, blink, lock = struct.unpack_from('<IIi', image_mem, pos + 8)
        if flink == blink == va + 8 and lock == -1:
            image_mem[pos:pos + 0x1C] = struct.pack('<Ii5I', 0, -1, 0, 0, 0, 0, 0)
            n += 1
        pos = image_mem.find(b'\x01\x00\x04\x00\x00\x00\x00\x00', pos + 1)
    return n


def base_name(name):
    """'@KfLowerIrql@4' / 'NtClose@4' -> 'KfLowerIrql' / 'NtClose'."""
    return name.lstrip('@').split('@')[0]


def find_all(mem, pat):
    out, pos = [], mem.find(pat)
    while pos != -1:
        out.append(pos)
        pos = mem.find(pat, pos + 1)
    return out
