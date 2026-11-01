/*
 * Win32 replacement for the Original Xbox kernel, linked straight into the
 * patched default_pc.exe by tools/build_flat_pe.py: the XBE's 125
 * xboxkrnl.exe ordinal thunks are pointed at the xb_* functions below, and
 * xb_ExeEntry becomes the PE entry point. No DLL involved.
 *
 * Built freestanding: clang --target=i686-pc-windows-gnu.
 */
#include "win32.h"

/* ------------------------------------------------------------------ */
/* libc bits the compiler may emit calls to                            */
/* ------------------------------------------------------------------ */

void* memcpy(void* d, const void* s, size_t n) { u8* a = d; const u8* b = s; while (n--) *a++ = *b++; return d; }
void* memset(void* d, int c, size_t n) { u8* a = d; while (n--) *a++ = (u8)c; return d; }
void* memmove(void* d, const void* s, size_t n) {
    u8* a = d; const u8* b = s;
    if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
int memcmp(const void* x, const void* y, size_t n) {
    const u8* a = x; const u8* b = y;
    for (; n; n--, a++, b++) if (*a != *b) return *a - *b;
    return 0;
}

u64 __udivmoddi4(u64 n, u64 d, u64* rem) {
    u64 q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) { r -= d; q |= 1ULL << i; }
    }
    if (rem) *rem = r;
    return q;
}
u64 __udivdi3(u64 n, u64 d) { return __udivmoddi4(n, d, NULL); }
u64 __umoddi3(u64 n, u64 d) { u64 r; __udivmoddi4(n, d, &r); return r; }
s64 __divdi3(s64 n, s64 d) {
    int neg = (n < 0) ^ (d < 0);
    u64 q = __udivmoddi4(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, NULL);
    return neg ? -(s64)q : (s64)q;
}
s64 __moddi3(s64 n, s64 d) {
    u64 r; __udivmoddi4(n < 0 ? -(u64)n : (u64)n, d < 0 ? -(u64)d : (u64)d, &r);
    return n < 0 ? -(s64)r : (s64)r;
}

static size_t xstrlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
static char xlower(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int xstrnicmp(const char* a, const char* b, size_t n) {
    for (; n; n--, a++, b++) {
        char x = xlower(*a), y = xlower(*b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

static HANDLE g_log = NULL;
static int    g_trace = 0;
static CRITICAL_SECTION g_logLock;

static int fmt_num(char* o, u32 v, int base, int width, char pad, int sgn) {
    char t[16]; int n = 0, len = 0;
    int neg = sgn && (s32)v < 0;
    if (neg) v = -(s32)v;
    do { u32 d = v % base; t[n++] = d < 10 ? '0' + d : 'a' + d - 10; v /= base; } while (v);
    if (neg) t[n++] = '-';
    while (n < width) t[n++] = pad;
    while (n) o[len++] = t[--n];
    return len;
}

static int xvsnprintf(char* out, int cap, const char* f, __builtin_va_list ap) {
    int n = 0;
    #define PUT(c) do { if (n < cap - 1) out[n] = (c); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        char pad = ' '; int width = 0;
        if (*f == '-') f++;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l' || *f == 'h') f++;
        char tmp[24]; int len;
        switch (*f) {
        case 'd': case 'i': len = fmt_num(tmp, __builtin_va_arg(ap, u32), 10, width, pad, 1); break;
        case 'u': len = fmt_num(tmp, __builtin_va_arg(ap, u32), 10, width, pad, 0); break;
        case 'x': case 'X': len = fmt_num(tmp, __builtin_va_arg(ap, u32), 16, width, pad, 0); break;
        case 'p': len = fmt_num(tmp, __builtin_va_arg(ap, u32), 16, 8, '0', 0); break;
        case 'c': tmp[0] = (char)__builtin_va_arg(ap, int); len = 1; break;
        case 's': {
            const char* s = __builtin_va_arg(ap, const char*);
            if (!s) s = "(null)";
            while (*s) PUT(*s++);
            continue;
        }
        case 'S': {
            const u16* s = __builtin_va_arg(ap, const u16*);
            if (!s) s = (const u16*)L"(null)";
            while (*s) PUT((char)*s++);
            continue;
        }
        case 'Z': { /* ANSI_STRING* */
            ANSI_STRING* s = __builtin_va_arg(ap, ANSI_STRING*);
            if (s && s->Buffer) for (int i = 0; i < s->Length; i++) PUT(s->Buffer[i]);
            continue;
        }
        default: PUT(*f); continue;
        }
        for (int i = 0; i < len; i++) PUT(tmp[i]);
    }
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return n;
    #undef PUT
}

static void klog_raw(const char* s, u32 len) {
    u32 w;
    EnterCriticalSection(&g_logLock);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, len, &w, NULL);
    if (g_log) WriteFile(g_log, s, len, &w, NULL);
    LeaveCriticalSection(&g_logLock);
}

static void klog(const char* f, ...) {
    char buf[1024];
    __builtin_va_list ap; __builtin_va_start(ap, f);
    int n = xvsnprintf(buf, sizeof(buf) - 1, f, ap);
    __builtin_va_end(ap);
    if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
    klog_raw(buf, n);
}

#define TRACE(...) do { if (g_trace) klog("[krnl %04x] " __VA_ARGS__); } while (0)
#define TID GetCurrentThreadId()
#define STUB(name) do { static int once; if (!once++) klog("[krnl] stub: %s\n", name); } while (0)

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static char g_gameDir[260];   /* "Z:\...\rom\extracted" (no trailing slash) */
static char g_saveDir[260];   /* "<gameDir>\_hdd"                           */
static HANDLE g_heap;
static s64 g_qpcFreq;

/* Xbox OBJECT_ATTRIBUTES uses ANSI names and no Length field */
typedef struct { HANDLE RootDirectory; ANSI_STRING* ObjectName; u32 Attributes; } XOBJECT_ATTRIBUTES;

/* Dispatcher objects living in game memory */
typedef struct { u8 Type, Absolute, Size, Inserted; s32 SignalState; void* Flink; void* Blink; } DISPATCHER_HEADER;
enum { NotificationEvent = 0, SynchronizationEvent = 1, MutantObject = 2, SemaphoreObject = 5,
       ThreadObject = 6, NotificationTimer = 8, SynchronizationTimer = 9 };
typedef struct { DISPATCHER_HEADER Header; s32 Limit; } KSEMAPHORE;

typedef struct KDPC {
    s16 Type; u8 Inserted, Padding;
    void* Flink; void* Blink;
    void (WINAPI* DeferredRoutine)(struct KDPC*, void*, void*, void*);
    void* DeferredContext; void* SystemArgument1; void* SystemArgument2;
} KDPC;

typedef struct {
    DISPATCHER_HEADER Header;
    u64 DueTime;           /* Xbox: ULARGE_INTEGER, we store absolute QPC ms */
    void* Flink; void* Blink;
    KDPC* Dpc;
    s32 Period;
} KTIMER;

/* One global lock + condition variable implements every in-memory
 * dispatcher wait (KEVENT/KTIMER/KSEMAPHORE/critical sections). */
static CRITICAL_SECTION   g_dispLock;
static CONDITION_VARIABLE g_dispCond;

/* Fake per-thread kernel structures. build_flat_pe.py rewrites the game's
 * Xbox KPCR accesses onto unused TEB UserReserved slots:
 *   fs:[0x20] Prcb          -> fs:[0xB0]
 *   fs:[0x28] CurrentThread -> fs:[0xB4]
 *   fs:[0x04] StackBase (only used for TLS lookups) -> fs:[0xAC] */
#define TEB_FAKE_STACKBASE 0xAC
#define TEB_PRCB           0xB0
#define TEB_KTHREAD        0xB4
typedef struct {
    u8  Prcb[0x300];     /* +0 = CurrentThread                        */
    u8  Thread[0x300];   /* KTHREAD: +0x28 TlsData, +0x44 Process      */
    u8* Tls;
    u32 TlsSize;
    HANDLE Handle;
    u32 Tid;
} XTHREAD;

static u8 g_kprocess[0x100];

static inline u8* get_teb(void) { u8* t; __asm__ volatile("movl %%fs:0x18, %0" : "=r"(t)); return t; }

static XTHREAD* thread_setup(u32 tlsSize) {
    u8* teb = get_teb();
    XTHREAD* xt = *(XTHREAD**)(teb + TEB_PRCB);
    if (!xt) {
        xt = HeapAlloc(g_heap, 8 /* HEAP_ZERO_MEMORY */, sizeof(XTHREAD));
        *(void**)(xt->Prcb + 0x00) = xt->Thread;
        *(void**)(xt->Thread + 0x44) = g_kprocess;
        *(u8*)(xt->Thread + 0x00) = ThreadObject;
        xt->Tid = TID;
        *(XTHREAD**)(teb + TEB_PRCB) = xt;      /* Prcb is the first member */
        *(void**)(teb + TEB_KTHREAD) = xt->Thread;
    }
    if (tlsSize && !xt->Tls) {
        xt->Tls = HeapAlloc(g_heap, 8, tlsSize + 16);
        xt->TlsSize = tlsSize;
        *(void**)(xt->Thread + 0x28) = xt->Tls;
        *(u8**)(teb + TEB_FAKE_STACKBASE) = xt->Tls + tlsSize; /* for [StackBase + idx*4] */
    }
    return xt;
}

static XTHREAD* cur_xthread(void) { return *(XTHREAD**)(get_teb() + TEB_PRCB); }

/* ------------------------------------------------------------------ */
/* Data exports                                                        */
/* ------------------------------------------------------------------ */

typedef struct { u32 Flags; u8 GpuRevision, McpRevision, Unknown3, Unknown4; } XBOX_HARDWARE_INFO;
typedef struct { u16 Major, Minor, Build, Qfe; } XBOX_KRNL_VERSION;

XBOX_HARDWARE_INFO xb_XboxHardwareInfo = { 0x00000020, 0xD4, 0xB1, 0, 0 };
XBOX_KRNL_VERSION  xb_XboxKrnlVersion  = { 1, 0, 5838, 1 };
u8   xb_XboxHDKey[16];
u8   xb_XboxSignatureKey[16];
u8   xb_XboxAlternateSignatureKeys[16][16];
u8   xb_IdexChannelObject[0x100];
u32  xb_HalDiskCachePartitionCount = 3;
u32  xb_HalBootSMCVideoMode = 1;
void* xb_LaunchDataPage = NULL;
volatile u32 xb_KeTickCount = 0;
u8   xb_ExEventObjectType[0x40];
u8   xb_IoFileObjectType[0x40];
u8   xb_PsThreadObjectType[0x40];
static char g_imageName[] = "\\Device\\CdRom0\\default.xbe";
ANSI_STRING xb_XeImageFileName = { sizeof(g_imageName) - 1, sizeof(g_imageName), g_imageName };

/* ------------------------------------------------------------------ */
/* Path translation: Xbox object names -> host NT paths                */
/* ------------------------------------------------------------------ */

#define MAX_LINKS 32
static struct { char name[64]; char target[128]; } g_links[MAX_LINKS];
static int g_linkCount;

static void link_add(const char* name, u32 nlen, const char* target, u32 tlen) {
    for (int i = 0; i < g_linkCount; i++)
        if (xstrlen(g_links[i].name) == nlen && !xstrnicmp(g_links[i].name, name, nlen)) {
            memcpy(g_links[i].target, target, tlen); g_links[i].target[tlen] = 0; return;
        }
    if (g_linkCount >= MAX_LINKS || nlen >= 64 || tlen >= 128) return;
    memcpy(g_links[g_linkCount].name, name, nlen); g_links[g_linkCount].name[nlen] = 0;
    memcpy(g_links[g_linkCount].target, target, tlen); g_links[g_linkCount].target[tlen] = 0;
    g_linkCount++;
}

static const struct { const char* dev; const char* sub; u64 rawSize; } g_devices[] = {
    { "\\Device\\CdRom0",               NULL, 0           },
    { "\\Device\\Harddisk0\\Partition0", "P0", 0x00080000ULL },  /* config area, cache DB */
    { "\\Device\\Harddisk0\\Partition1", "E",  0x131F00000ULL },
    { "\\Device\\Harddisk0\\Partition2", "C",  0x01F400000ULL },
    { "\\Device\\Harddisk0\\Partition3", "X",  0x02EE00000ULL },
    { "\\Device\\Harddisk0\\Partition4", "Y",  0x02EE00000ULL },
    { "\\Device\\Harddisk0\\Partition5", "Z",  0x02EE00000ULL },
    { "\\Device\\Harddisk0\\Partition6", "F",  0x02EE00000ULL },
};
#define NUM_DEVICES (sizeof(g_devices) / sizeof(g_devices[0]))

/* Raw (volume) opens of a partition are backed by "<saveDir>\<sub>.raw".
 * XAPI reads/writes the cache DB and formats FATX on them; normal file
 * access goes to the "<saveDir>\<sub>" directory. */
#define MAX_RAW 16
static struct { HANDLE h; int dev; } g_raw[MAX_RAW];
static CRITICAL_SECTION g_rawLock;

static int raw_dev(HANDLE h) {
    int d = -1;
    EnterCriticalSection(&g_rawLock);
    for (int i = 0; i < MAX_RAW; i++) if (g_raw[i].h == h) { d = g_raw[i].dev; break; }
    LeaveCriticalSection(&g_rawLock);
    return d;
}
static void raw_track(HANDLE h, int dev) {
    EnterCriticalSection(&g_rawLock);
    for (int i = 0; i < MAX_RAW; i++) if (!g_raw[i].h) { g_raw[i].h = h; g_raw[i].dev = dev; break; }
    LeaveCriticalSection(&g_rawLock);
}
static void raw_untrack(HANDLE h) {
    EnterCriticalSection(&g_rawLock);
    for (int i = 0; i < MAX_RAW; i++) if (g_raw[i].h == h) g_raw[i].h = NULL;
    LeaveCriticalSection(&g_rawLock);
}

/* Resolve an Xbox path into "\??\<host>\..." UTF-16. Returns 0 if unknown.
 * *raw receives the device index for a raw partition open, else -1. */
static int translate_path(const char* in, u32 len, u16* out, u32 cap, int* raw) {
    char a[400], b[400];
    if (len >= sizeof(a)) return 0;
    memcpy(a, in, len); a[len] = 0;

    /* Normalise "X:\..." to "\??\X:\..." */
    if (len >= 2 && a[1] == ':') { memcpy(b, "\\??\\", 4); memcpy(b + 4, a, len + 1); memcpy(a, b, len + 5); len += 4; }

    /* Expand symbolic links (\??\D: -> \Device\CdRom0...) */
    for (int pass = 0; pass < 4; pass++) {
        int hit = 0;
        for (int i = 0; i < g_linkCount; i++) {
            u32 nl = xstrlen(g_links[i].name);
            if (len >= nl && !xstrnicmp(a, g_links[i].name, nl) && (a[nl] == '\\' || a[nl] == 0)) {
                u32 tl = xstrlen(g_links[i].target);
                if (tl + len - nl >= sizeof(b)) return 0;
                memcpy(b, g_links[i].target, tl);
                memcpy(b + tl, a + nl, len - nl + 1);
                len = tl + len - nl; memcpy(a, b, len + 1);
                hit = 1; break;
            }
        }
        if (!hit) break;
    }

    const char* host = NULL; u32 skip = 0; const char* sub = NULL;
    *raw = -1;
    for (u32 i = 0; i < NUM_DEVICES; i++) {
        u32 dl = xstrlen(g_devices[i].dev);
        if (len >= dl && !xstrnicmp(a, g_devices[i].dev, dl) && (a[dl] == '\\' || a[dl] == 0)) {
            sub = g_devices[i].sub; host = sub ? g_saveDir : g_gameDir; skip = dl;
            if (sub && a[dl] == 0) *raw = i;
            break;
        }
    }
    if (!host) return 0;

    u32 n = 0;
    #define W(c) do { if (n + 1 >= cap) return 0; out[n++] = (u16)(u8)(c); } while (0)
    W('\\'); W('?'); W('?'); W('\\');
    for (const char* p = host; *p; p++) W(*p);
    if (sub) { W('\\'); for (const char* p = sub; *p; p++) W(*p); }
    if (*raw >= 0) { W('.'); W('r'); W('a'); W('w'); }
    for (u32 i = skip; i < len; i++) W(a[i]);
    out[n] = 0;
    #undef W
    return n;
}

/* Build native OBJECT_ATTRIBUTES from Xbox ones. */
typedef struct { NT_OBJECT_ATTRIBUTES oa; UNICODE_STRING us; u16 buf[600]; int raw; } NATIVE_OA;

static NTSTATUS convert_oa(XOBJECT_ATTRIBUTES* x, NATIVE_OA* n) {
    memset(&n->oa, 0, sizeof(n->oa));
    n->oa.Length = sizeof(n->oa);
    n->oa.Attributes = x->Attributes & ~0x40u; /* OBJ_CASE_INSENSITIVE is fine either way */
    n->oa.Attributes |= 0x40;
    n->oa.ObjectName = &n->us;
    ANSI_STRING* s = x->ObjectName;
    u32 len = s ? s->Length : 0;
    n->raw = -1;
    HANDLE root = x->RootDirectory;
    /* ObDosDevicesDirectory / ObWin32NamedObjectsDirectory pseudo roots */
    if (root == (HANDLE)-3 || root == (HANDLE)-4) root = NULL;
    if (root) {
        /* relative open against a real native directory handle */
        u32 i;
        for (i = 0; i < len && i < 599; i++) n->buf[i] = (u8)s->Buffer[i];
        n->buf[i] = 0;
        n->oa.RootDirectory = root;
        n->us.Buffer = n->buf; n->us.Length = (u16)(i * 2); n->us.MaximumLength = (u16)(i * 2 + 2);
        return STATUS_SUCCESS;
    }
    int w = translate_path(s ? s->Buffer : "", len, n->buf, 600, &n->raw);
    if (!w) {
        klog("[krnl] unknown path: %Z\n", s);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    n->us.Buffer = n->buf; n->us.Length = (u16)(w * 2); n->us.MaximumLength = (u16)(w * 2 + 2);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Av*                                                                 */
/* ------------------------------------------------------------------ */

void* WINAPI xb_AvGetSavedDataAddress(void) { return NULL; }
void WINAPI xb_AvSetSavedDataAddress(void* a) { (void)a; }
void WINAPI xb_AvSendTVEncoderOption(void* reg, u32 option, u32 param, u32* result) {
    (void)reg; (void)param;
    TRACE("AvSendTVEncoderOption(%u, %x)\n", TID, option, param);
    if (result) *result = (option == 6 /* AV_QUERY_AV_CAPABILITIES */)
        ? 0x00000300 | 3      /* AV_STANDARD_PAL_I | AV_PACK_SCART */
        : 0;
}
u32 WINAPI xb_AvSetDisplayMode(void* reg, u32 step, u32 mode, u32 format, u32 pitch, u32 fb) {
    (void)reg; (void)step; (void)pitch; (void)fb;
    TRACE("AvSetDisplayMode(mode=%x fmt=%x)\n", TID, mode, format);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Dbg*                                                                */
/* ------------------------------------------------------------------ */

u32 xb_DbgPrint(const char* fmt, ...) {
    char buf[1024];
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int n = xvsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    __builtin_va_end(ap);
    if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
    klog_raw("[game] ", 7);
    klog_raw(buf, n);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Ex*                                                                 */
/* ------------------------------------------------------------------ */

void* WINAPI xb_ExAllocatePoolWithTag(u32 size, u32 tag) { (void)tag; return HeapAlloc(g_heap, 0, size); }
void  WINAPI xb_ExFreePool(void* p) { HeapFree(g_heap, 0, p); }
u32   WINAPI xb_ExQueryPoolBlockSize(void* p) { return HeapSize(g_heap, 0, p); }

NTSTATUS WINAPI xb_ExQueryNonVolatileSetting(u32 index, u32* type, void* value, u32 len, u32* outLen) {
    u32 v = 0;
    switch (index) {
    case 0x07:  v = 1; break;            /* XC_LANGUAGE: English            */
    case 0x08:  v = 0; break;            /* XC_VIDEO flags                  */
    case 0x09:  v = 0; break;            /* XC_AUDIO flags                  */
    case 0x103: v = 0x00000300; break;   /* XC_FACTORY_AV_REGION: PAL-I     */
    case 0x104: v = 4; break;            /* XC_FACTORY_GAME_REGION: Europe  */
    default:    v = 0; break;
    }
    TRACE("ExQueryNonVolatileSetting(%x) -> %x\n", TID, index, v);
    if (type) *type = 4; /* REG_DWORD */
    if (value && len >= 4) *(u32*)value = v;
    else if (value && len) memset(value, 0, len);
    if (outLen) *outLen = 4;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Hal*                                                                */
/* ------------------------------------------------------------------ */

u32  WINAPI xb_HalGetInterruptVector(u32 level, u8* irql) { if (irql) *irql = (u8)(0x1B - level); return 0x30 + level; }
void WINAPI xb_HalReadWritePCISpace(u32 bus, u32 slot, u32 reg, void* buf, u32 len, BOOL write) {
    TRACE("HalReadWritePCISpace(bus=%u slot=%x reg=%x len=%u w=%d)\n", TID, bus, slot, reg, len, write);
    if (!write) memset(buf, 0, len);
}
void WINAPI xb_HalRegisterShutdownNotification(void* r, BOOL reg) { (void)r; (void)reg; }
void WINAPI xb_HalReturnToFirmware(u32 routine) { klog("[krnl] HalReturnToFirmware(%u) - exiting\n", routine); ExitProcess(0); }
BOOL WINAPI xb_HalIsResetOrShutdownPending(void) { return FALSE; }
void WINAPI xb_HalInitiateShutdown(void) { klog("[krnl] HalInitiateShutdown\n"); ExitProcess(0); }

/* ------------------------------------------------------------------ */
/* Io*                                                                 */
/* ------------------------------------------------------------------ */

NTSTATUS WINAPI xb_IoCreateDevice(void* drv, u32 extSize, ANSI_STRING* name, u32 type, BOOL excl, void** dev) {
    (void)drv; (void)type; (void)excl;
    TRACE("IoCreateDevice(%Z)\n", TID, name);
    u8* d = HeapAlloc(g_heap, 8, 0x80 + extSize);
    *(void**)(d + 0x18) = extSize ? d + 0x80 : NULL; /* DeviceExtension */
    *dev = d;
    return STATUS_SUCCESS;
}
NTSTATUS WINAPI xb_IoCreateSymbolicLink(ANSI_STRING* link, ANSI_STRING* dev) {
    klog("[krnl] IoCreateSymbolicLink(%Z -> %Z)\n", link, dev);
    link_add(link->Buffer, link->Length, dev->Buffer, dev->Length);
    return STATUS_SUCCESS;
}
NTSTATUS WINAPI xb_IoDeleteSymbolicLink(ANSI_STRING* link) { (void)link; return STATUS_SUCCESS; }
NTSTATUS WINAPI xb_IoInvalidDeviceRequest(void* dev, void* irp) { (void)dev; (void)irp; return STATUS_INVALID_DEVICE_REQUEST; }
void WINAPI xb_IoStartNextPacket(void* dev) { (void)dev; }
void WINAPI xb_IoStartPacket(void* dev, void* irp, u32* key) { (void)dev; (void)irp; (void)key; }
void FASTCALL xb_IofCompleteRequest(void* irp, s8 boost) { (void)irp; (void)boost; }
void WINAPI xb_IoMarkIrpMustComplete(void* irp) { (void)irp; }

/* ------------------------------------------------------------------ */
/* Ke* - dispatcher objects in game memory                             */
/* ------------------------------------------------------------------ */

static u64 now_ms(void) { s64 c; QueryPerformanceCounter(&c); return (u64)c * 1000 / (u64)g_qpcFreq; }
static u64 now_100ns(void) { s64 c; QueryPerformanceCounter(&c); return (u64)c * 10000000ULL / (u64)g_qpcFreq; }

/* Convert an NT timeout (negative = relative 100ns, positive = absolute) to ms */
static u32 timeout_ms(s64* t) {
    if (!t) return INFINITE;
    s64 v = *t;
    if (v < 0) return (u32)((-v + 9999) / 10000);
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    s64 nowT = ((s64)ft.hi << 32) | ft.lo;
    return v > nowT ? (u32)((v - nowT + 9999) / 10000) : 0;
}

static int try_acquire(DISPATCHER_HEADER* h) {
    if (h->SignalState <= 0) return 0;
    switch (h->Type) {
    case SynchronizationEvent: case SynchronizationTimer: h->SignalState = 0; break;
    case SemaphoreObject: h->SignalState--; break;
    default: break;
    }
    return 1;
}

static NTSTATUS disp_wait(DISPATCHER_HEADER* h, s64* timeout) {
    u32 ms = timeout_ms(timeout);
    u64 deadline = ms == INFINITE ? 0 : now_ms() + ms;
    EnterCriticalSection(&g_dispLock);
    while (!try_acquire(h)) {
        u32 wait = INFINITE;
        if (ms != INFINITE) {
            u64 n = now_ms();
            if (n >= deadline) { LeaveCriticalSection(&g_dispLock); return STATUS_TIMEOUT; }
            wait = (u32)(deadline - n);
        }
        SleepConditionVariableCS(&g_dispCond, &g_dispLock, wait);
    }
    LeaveCriticalSection(&g_dispLock);
    return STATUS_SUCCESS;
}

static void disp_signal(DISPATCHER_HEADER* h, s32 state) {
    EnterCriticalSection(&g_dispLock);
    h->SignalState = state;
    WakeAllConditionVariable(&g_dispCond);
    LeaveCriticalSection(&g_dispLock);
}

/* DPC queue + timers serviced by one "interrupt" thread */
#define MAX_TIMERS 64
#define MAX_DPCS   256
static KTIMER* g_timers[MAX_TIMERS];
static KDPC*   g_dpcq[MAX_DPCS];
static u32     g_dpcHead, g_dpcTail;
static CRITICAL_SECTION g_dpcLock;

BOOL WINAPI xb_KeInsertQueueDpc(KDPC* dpc, void* a1, void* a2) {
    BOOL ok = FALSE;
    EnterCriticalSection(&g_dpcLock);
    if (!dpc->Inserted && g_dpcTail - g_dpcHead < MAX_DPCS) {
        dpc->Inserted = 1; dpc->SystemArgument1 = a1; dpc->SystemArgument2 = a2;
        g_dpcq[g_dpcTail++ % MAX_DPCS] = dpc; ok = TRUE;
    }
    LeaveCriticalSection(&g_dpcLock);
    return ok;
}
BOOL WINAPI xb_KeRemoveQueueDpc(KDPC* dpc) {
    BOOL was;
    EnterCriticalSection(&g_dpcLock);
    was = dpc->Inserted; dpc->Inserted = 0;
    LeaveCriticalSection(&g_dpcLock);
    return was;
}
void WINAPI xb_KeInitializeDpc(KDPC* dpc, void* routine, void* ctx) {
    memset(dpc, 0, sizeof(*dpc));
    dpc->Type = 19; dpc->DeferredRoutine = routine; dpc->DeferredContext = ctx;
}

void WINAPI xb_KeInitializeTimerEx(KTIMER* t, u32 type) {
    memset(t, 0, sizeof(*t));
    t->Header.Type = (u8)(NotificationTimer + type);
    t->Header.Size = sizeof(KTIMER) / 4;
}
BOOL WINAPI xb_KeCancelTimer(KTIMER* t) {
    BOOL was = FALSE;
    EnterCriticalSection(&g_dpcLock);
    for (int i = 0; i < MAX_TIMERS; i++) if (g_timers[i] == t) { g_timers[i] = NULL; was = TRUE; }
    t->Header.Inserted = 0;
    LeaveCriticalSection(&g_dpcLock);
    return was;
}
BOOL WINAPI xb_KeSetTimerEx(KTIMER* t, s64 due, s32 period, KDPC* dpc) {
    BOOL was = xb_KeCancelTimer(t);
    u32 ms = timeout_ms(&due);
    EnterCriticalSection(&g_dpcLock);
    t->DueTime = now_ms() + ms; t->Period = period; t->Dpc = dpc;
    t->Header.SignalState = 0; t->Header.Inserted = 1;
    for (int i = 0; i < MAX_TIMERS; i++) if (!g_timers[i]) { g_timers[i] = t; break; }
    LeaveCriticalSection(&g_dpcLock);
    return was;
}
BOOL WINAPI xb_KeSetTimer(KTIMER* t, s64 due, KDPC* dpc) { return xb_KeSetTimerEx(t, due, 0, dpc); }

static u32 WINAPI interrupt_thread(void* arg) {
    (void)arg;
    thread_setup(0);
    u64 start = now_ms();
    for (;;) {
        u64 n = now_ms();
        xb_KeTickCount = (u32)(n - start);
        /* expire timers */
        EnterCriticalSection(&g_dpcLock);
        for (int i = 0; i < MAX_TIMERS; i++) {
            KTIMER* t = g_timers[i];
            if (!t || n < t->DueTime) continue;
            if (t->Period) t->DueTime = n + t->Period; else { g_timers[i] = NULL; t->Header.Inserted = 0; }
            disp_signal(&t->Header, 1);
            if (t->Dpc && !t->Dpc->Inserted && g_dpcTail - g_dpcHead < MAX_DPCS) {
                t->Dpc->Inserted = 1; g_dpcq[g_dpcTail++ % MAX_DPCS] = t->Dpc;
            }
        }
        LeaveCriticalSection(&g_dpcLock);
        /* run DPCs */
        for (;;) {
            KDPC* d = NULL;
            EnterCriticalSection(&g_dpcLock);
            while (g_dpcHead != g_dpcTail && !d) {
                d = g_dpcq[g_dpcHead++ % MAX_DPCS];
                if (!d->Inserted) d = NULL; else d->Inserted = 0;
            }
            LeaveCriticalSection(&g_dpcLock);
            if (!d) break;
            d->DeferredRoutine(d, d->DeferredContext, d->SystemArgument1, d->SystemArgument2);
        }
        Sleep(1);
    }
    return 0;
}

void WINAPI xb_KeInitializeInterrupt(void* i, void* r, void* c, u32 v, u8 irql, u32 mode, BOOL share) {
    (void)i; (void)r; (void)c; (void)v; (void)irql; (void)mode; (void)share;
    TRACE("KeInitializeInterrupt(vector=%x)\n", TID, v);
}
BOOL WINAPI xb_KeConnectInterrupt(void* i) { (void)i; return TRUE; }
BOOL WINAPI xb_KeDisconnectInterrupt(void* i) { (void)i; return TRUE; }
BOOL WINAPI xb_KeSynchronizeExecution(void* i, BOOL (WINAPI* r)(void*), void* ctx) { (void)i; return r(ctx); }

NTSTATUS WINAPI xb_KeDelayExecutionThread(s8 mode, BOOL alertable, s64* interval) {
    (void)mode; return NtDelayExecution(alertable, interval);
}
void WINAPI xb_KeQuerySystemTime(s64* t) { FILETIME ft; GetSystemTimeAsFileTime(&ft); *t = ((s64)ft.hi << 32) | ft.lo; }
u8 WINAPI xb_KeRaiseIrqlToDpcLevel(void) { return 0; }
u8 FASTCALL xb_KfRaiseIrql(u8 irql) { (void)irql; return 0; }
void FASTCALL xb_KfLowerIrql(u8 irql) { (void)irql; }
NTSTATUS WINAPI xb_KeSaveFloatingPointState(void* s) { (void)s; return STATUS_SUCCESS; }
NTSTATUS WINAPI xb_KeRestoreFloatingPointState(void* s) { (void)s; return STATUS_SUCCESS; }
s32 WINAPI xb_KeSetBasePriorityThread(void* thr, s32 inc) { (void)thr; (void)inc; return 0; }
void WINAPI xb_KeStallExecutionProcessor(u32 us) {
    u64 end = now_100ns() + (u64)us * 10;
    while (now_100ns() < end) __asm__ volatile("pause");
}
void WINAPI xb_KeBugCheck(u32 code) { klog("[krnl] KeBugCheck(%x)\n", code); ExitProcess(code); }

s32 WINAPI xb_KeSetEvent(DISPATCHER_HEADER* ev, s32 inc, BOOL wait) {
    (void)inc; (void)wait;
    s32 old = ev->SignalState;
    disp_signal(ev, 1);
    return old;
}

NTSTATUS WINAPI xb_KeWaitForSingleObject(DISPATCHER_HEADER* obj, u32 reason, s8 mode, BOOL alertable, s64* timeout) {
    (void)reason; (void)mode; (void)alertable;
    return disp_wait(obj, timeout);
}

/* ------------------------------------------------------------------ */
/* Mm*                                                                 */
/* ------------------------------------------------------------------ */

static u32 host_protect(u32 p) {
    p &= ~(PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return p ? p : PAGE_READWRITE;
}

/* Xbox physical RAM (64 MB) is mirrored at 0x80000000: contiguous memory is
 * VA = 0x80000000 | PA, which D3D relies on when it hands addresses to the
 * GPU. We reserve that window and run a first-fit allocator over it. */
#define PHYS_BASE   0x80000000u
#define PHYS_SIZE   0x04000000u
#define PHYS_ALLOC_LO 0x00100000u            /* below: kernel image area */
#define PHYS_INSTANCE 0x03FF0000u            /* top 64 KB: NV2A instance memory */
#define MAX_PBLOCKS 4096
static struct { u32 pa, size; } g_pblocks[MAX_PBLOCKS]; /* sorted by pa */
static int g_npblocks;
static CRITICAL_SECTION g_physLock;

static void* phys_alloc(u32 size, u32 lo, u32 hi, u32 align) {
    if (!align) align = 0x1000;
    size = (size + 0xFFF) & ~0xFFFu;
    if (lo < PHYS_ALLOC_LO) lo = PHYS_ALLOC_LO;
    if (hi > PHYS_INSTANCE - 1) hi = PHYS_INSTANCE - 1;
    void* res = NULL;
    EnterCriticalSection(&g_physLock);
    u32 cand = (lo + align - 1) & ~(align - 1);
    int i = 0;
    for (; i <= g_npblocks; i++) {
        u32 limit = i < g_npblocks ? g_pblocks[i].pa : hi + 1;
        if (i < g_npblocks && g_pblocks[i].pa + g_pblocks[i].size <= cand) continue;
        if (cand + size <= limit && cand + size - 1 <= hi) break;
        if (i == g_npblocks) { i = -1; break; }
        cand = (g_pblocks[i].pa + g_pblocks[i].size + align - 1) & ~(align - 1);
    }
    if (i >= 0 && g_npblocks < MAX_PBLOCKS) {
        memmove(&g_pblocks[i + 1], &g_pblocks[i], (g_npblocks - i) * sizeof(g_pblocks[0]));
        g_pblocks[i].pa = cand; g_pblocks[i].size = size; g_npblocks++;
        res = (void*)(PHYS_BASE | cand);
        memset(res, 0, size);
    }
    LeaveCriticalSection(&g_physLock);
    return res;
}

static u32 phys_free(void* va) {
    u32 pa = (u32)va & (PHYS_SIZE - 1), sz = 0;
    EnterCriticalSection(&g_physLock);
    for (int i = 0; i < g_npblocks; i++)
        if (g_pblocks[i].pa == pa) {
            sz = g_pblocks[i].size;
            memmove(&g_pblocks[i], &g_pblocks[i + 1], (g_npblocks - i - 1) * sizeof(g_pblocks[0]));
            g_npblocks--; break;
        }
    LeaveCriticalSection(&g_physLock);
    return sz;
}

static int is_phys(void* va) { return (u32)va >= PHYS_BASE && (u32)va < PHYS_BASE + PHYS_SIZE; }

void* WINAPI xb_MmAllocateContiguousMemoryEx(u32 size, u32 lo, u32 hi, u32 align, u32 protect) {
    (void)protect;
    void* p = phys_alloc(size, lo, hi, align);
    TRACE("MmAllocateContiguousMemoryEx(%x, %x-%x, align=%x) -> %p\n", TID, size, lo, hi, align, p);
    return p;
}
void* WINAPI xb_MmAllocateContiguousMemory(u32 size) {
    return xb_MmAllocateContiguousMemoryEx(size, 0, PHYS_SIZE - 1, 0, PAGE_READWRITE);
}
void  WINAPI xb_MmFreeContiguousMemory(void* p) { if (is_phys(p)) phys_free(p); else VirtualFree(p, 0, MEM_RELEASE); }
void* WINAPI xb_MmClaimGpuInstanceMemory(u32 size, u32* padding) {
    if (padding) *padding = 0x10000;
    TRACE("MmClaimGpuInstanceMemory(%x)\n", TID, size);
    return (void*)(PHYS_BASE + PHYS_SIZE - 0x10000);
}
u32  WINAPI xb_MmGetPhysicalAddress(void* va) {
    if (is_phys(va)) return (u32)va & (PHYS_SIZE - 1);
    STUB("MmGetPhysicalAddress(non-contiguous)");
    return (u32)va;
}
void WINAPI xb_MmLockUnlockBufferPages(void* b, u32 n, BOOL unlock) { (void)b; (void)n; (void)unlock; }
void WINAPI xb_MmLockUnlockPhysicalPage(u32 pa, BOOL unlock) { (void)pa; (void)unlock; }
void WINAPI xb_MmPersistContiguousMemory(void* b, u32 n, BOOL persist) { (void)b; (void)n; (void)persist; }
u32  WINAPI xb_MmQueryAddressProtect(void* va) {
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(va, &m, sizeof(m)) ? m.Protect : 0;
}
u32  WINAPI xb_MmQueryAllocationSize(void* va) {
    if (is_phys(va)) {
        u32 pa = (u32)va & (PHYS_SIZE - 1), sz = 0;
        EnterCriticalSection(&g_physLock);
        for (int i = 0; i < g_npblocks; i++) if (g_pblocks[i].pa == pa) sz = g_pblocks[i].size;
        LeaveCriticalSection(&g_physLock);
        return sz;
    }
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(va, &m, sizeof(m))) return 0;
    u8* base = m.AllocationBase; u8* p = base; u32 total = 0;
    while (VirtualQuery(p, &m, sizeof(m)) && m.AllocationBase == base && m.State == MEM_COMMIT) {
        total += m.RegionSize; p += m.RegionSize;
    }
    return total;
}
void WINAPI xb_MmSetAddressProtect(void* b, u32 n, u32 protect) {
    if (is_phys(b)) return; /* keep the physical window RW */
    u32 old; VirtualProtect(b, n, host_protect(protect), &old);
}

/* ------------------------------------------------------------------ */
/* Nt* - files                                                         */
/* ------------------------------------------------------------------ */

static NTSTATUS raw_open(HANDLE* h, NATIVE_OA* n, IO_STATUS_BLOCK* iosb) {
    /* GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE, share rw, FILE_OPEN_IF, non-dir + sync io */
    NTSTATUS s = NtCreateFile(h, 0xC0100000, &n->oa, iosb, NULL, 0x80, 3, 3, 0x60, NULL, 0);
    if (NT_SUCCESS(s)) raw_track(*h, n->raw);
    klog("[krnl] raw open %s -> %x\n", g_devices[n->raw].dev, s);
    return s;
}

NTSTATUS WINAPI xb_NtCreateFile(HANDLE* h, u32 access, XOBJECT_ATTRIBUTES* xoa, IO_STATUS_BLOCK* iosb,
                                s64* allocSize, u32 attrs, u32 share, u32 disp, u32 options) {
    NATIVE_OA n;
    NTSTATUS s = convert_oa(xoa, &n);
    if (NT_SUCCESS(s) && n.raw >= 0) return raw_open(h, &n, iosb);
    if (NT_SUCCESS(s))
        s = NtCreateFile(h, access, &n.oa, iosb, allocSize, attrs, share, disp, options, NULL, 0);
    TRACE("NtCreateFile(%Z, disp=%u) -> %x h=%p\n", TID, xoa->ObjectName, disp, s, NT_SUCCESS(s) ? *h : 0);
    if (!NT_SUCCESS(s) && iosb) iosb->Status = s;
    return s;
}
NTSTATUS WINAPI xb_NtOpenFile(HANDLE* h, u32 access, XOBJECT_ATTRIBUTES* xoa, IO_STATUS_BLOCK* iosb, u32 share, u32 options) {
    NATIVE_OA n;
    NTSTATUS s = convert_oa(xoa, &n);
    if (NT_SUCCESS(s) && n.raw >= 0) s = raw_open(h, &n, iosb);
    else if (NT_SUCCESS(s)) s = NtOpenFile(h, access, &n.oa, iosb, share, options);
    TRACE("NtOpenFile(%Z) -> %x\n", TID, xoa->ObjectName, s);
    return s;
}
NTSTATUS WINAPI xb_NtReadFile(HANDLE h, HANDLE ev, void* apc, void* ctx, IO_STATUS_BLOCK* iosb, void* buf, u32 len, s64* off) {
    NTSTATUS s = NtReadFile(h, ev, apc, ctx, iosb, buf, len, off, NULL);
    if (s == (NTSTATUS)0xC0000011 /* END_OF_FILE */ && raw_dev(h) >= 0) {
        /* unwritten sectors of a raw partition read as zeros */
        memset(buf, 0, len);
        iosb->Status = s = STATUS_SUCCESS; iosb->Information = len;
    }
    return s;
}
NTSTATUS WINAPI xb_NtWriteFile(HANDLE h, HANDLE ev, void* apc, void* ctx, IO_STATUS_BLOCK* iosb, void* buf, u32 len, s64* off) {
    return NtWriteFile(h, ev, apc, ctx, iosb, buf, len, off, NULL);
}
NTSTATUS WINAPI xb_NtQueryInformationFile(HANDLE h, IO_STATUS_BLOCK* iosb, void* info, u32 len, u32 cls) {
    return NtQueryInformationFile(h, iosb, info, len, cls);
}
NTSTATUS WINAPI xb_NtSetInformationFile(HANDLE h, IO_STATUS_BLOCK* iosb, void* info, u32 len, u32 cls) {
    if (cls == 10 /* FileRenameInformation: ANSI on Xbox */ || cls == 11 /* FileLinkInformation */) {
        STUB("NtSetInformationFile(rename)");
        return STATUS_NOT_IMPLEMENTED;
    }
    return NtSetInformationFile(h, iosb, info, len, cls);
}
NTSTATUS WINAPI xb_NtQueryVolumeInformationFile(HANDLE h, IO_STATUS_BLOCK* iosb, void* info, u32 len, u32 cls) {
    NTSTATUS s = NtQueryVolumeInformationFile(h, iosb, info, len, cls);
    /* XAPI validates its partitions as FATX: 512-byte sectors, 16 KB clusters.
     * Re-express the host volume size in that geometry. */
    if (NT_SUCCESS(s) && (cls == 3 /* FileFsSizeInformation */ || cls == 7 /* FileFsFullSizeInformation */)) {
        u32* p = info;
        u32 spc = cls == 3 ? 4 : 6, bps = spc + 1;          /* dword index of the two fields */
        u64 cluster = (u64)p[spc] * p[bps];
        u64* units = (u64*)info;
        int n = cls == 3 ? 2 : 3;
        for (int i = 0; i < n; i++) units[i] = units[i] * cluster / 0x4000;
        p[spc] = 32; p[bps] = 512;
    }
    return s;
}
NTSTATUS WINAPI xb_NtQueryFullAttributesFile(XOBJECT_ATTRIBUTES* xoa, void* info) {
    NATIVE_OA n;
    NTSTATUS s = convert_oa(xoa, &n);
    if (NT_SUCCESS(s)) s = NtQueryFullAttributesFile(&n.oa, info);
    TRACE("NtQueryFullAttributesFile(%Z) -> %x\n", TID, xoa->ObjectName, s);
    return s;
}
NTSTATUS WINAPI xb_NtFlushBuffersFile(HANDLE h, IO_STATUS_BLOCK* iosb) { return NtFlushBuffersFile(h, iosb); }
NTSTATUS WINAPI xb_NtDeviceIoControlFile(HANDLE h, HANDLE ev, void* apc, void* ctx, IO_STATUS_BLOCK* iosb,
                                         u32 code, void* in, u32 inLen, void* out, u32 outLen) {
    (void)ev; (void)apc; (void)ctx; (void)in; (void)inLen;
    int d = raw_dev(h);
    u64 size = d >= 0 ? g_devices[d].rawSize : 0;
    if (out && outLen) memset(out, 0, outLen);
    switch (code) {
    case 0x70000: /* IOCTL_DISK_GET_DRIVE_GEOMETRY */
        if (outLen >= 24) {
            u32* g = out;
            u64 sectors = size / 512;
            *(u64*)g = sectors / (255 * 63) + 1;  /* Cylinders */
            g[2] = 12;                             /* FixedMedia */
            g[3] = 255; g[4] = 63; g[5] = 512;     /* tracks, sectors/track, bytes/sector */
        }
        break;
    case 0x74004: /* IOCTL_DISK_GET_PARTITION_INFO */
        if (outLen >= 32) {
            u8* p = out;
            *(u64*)(p + 8) = size;                 /* PartitionLength */
            *(u32*)(p + 20) = d;                   /* PartitionNumber */
            p[24] = 0x06; p[26] = 1;               /* PartitionType, RecognizedPartition */
        }
        break;
    default:
        klog("[krnl] NtDeviceIoControlFile(code=%x dev=%d) - zeroed\n", code, d);
    }
    if (iosb) { iosb->Status = STATUS_SUCCESS; iosb->Information = outLen; }
    return STATUS_SUCCESS;
}
NTSTATUS WINAPI xb_NtFsControlFile(HANDLE h, HANDLE ev, void* apc, void* ctx, IO_STATUS_BLOCK* iosb,
                                   u32 code, void* in, u32 inLen, void* out, u32 outLen) {
    (void)h; (void)ev; (void)apc; (void)ctx; (void)in; (void)inLen; (void)out; (void)outLen;
    klog("[krnl] NtFsControlFile(code=%x) - faked\n", code);
    if (iosb) { iosb->Status = STATUS_SUCCESS; iosb->Information = 0; }
    return STATUS_SUCCESS;
}

/* Symbolic link objects: pseudo handles 0x5A000000 | index */
#define LINK_HANDLE_BASE 0x5A000000u
NTSTATUS WINAPI xb_NtOpenSymbolicLinkObject(HANDLE* h, XOBJECT_ATTRIBUTES* xoa) {
    ANSI_STRING* s = xoa->ObjectName;
    for (int i = 0; i < g_linkCount; i++)
        if (xstrlen(g_links[i].name) == s->Length && !xstrnicmp(g_links[i].name, s->Buffer, s->Length)) {
            *h = (HANDLE)(LINK_HANDLE_BASE | i);
            return STATUS_SUCCESS;
        }
    TRACE("NtOpenSymbolicLinkObject(%Z) -> not found\n", TID, s);
    return STATUS_OBJECT_NAME_NOT_FOUND;
}
NTSTATUS WINAPI xb_NtQuerySymbolicLinkObject(HANDLE h, ANSI_STRING* target, u32* retLen) {
    u32 i = (u32)h & 0xFFFF;
    if (((u32)h & 0xFFFF0000) != LINK_HANDLE_BASE || (int)i >= g_linkCount) return STATUS_INVALID_HANDLE;
    u32 l = xstrlen(g_links[i].target);
    if (retLen) *retLen = l;
    if (target->MaximumLength < l) return STATUS_BUFFER_TOO_SMALL;
    memcpy(target->Buffer, g_links[i].target, l);
    target->Length = (u16)l;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Nt* - objects, memory, threads                                      */
/* ------------------------------------------------------------------ */

static const char* oa_name(XOBJECT_ATTRIBUTES* xoa, char* buf) {
    if (!xoa || !xoa->ObjectName || !xoa->ObjectName->Length) return NULL;
    u32 l = xoa->ObjectName->Length; if (l > 120) l = 120;
    memcpy(buf, "Local\\xbox_", 11);
    for (u32 i = 0; i < l; i++) { char c = xoa->ObjectName->Buffer[i]; buf[11 + i] = c == '\\' ? '_' : c; }
    buf[11 + l] = 0;
    return buf;
}

NTSTATUS WINAPI xb_NtClose(HANDLE h) {
    if (((u32)h & 0xFFFF0000) == LINK_HANDLE_BASE) return STATUS_SUCCESS;
    raw_untrack(h);
    return NtClose(h);
}
NTSTATUS WINAPI xb_NtCreateEvent(HANDLE* h, XOBJECT_ATTRIBUTES* xoa, u32 type, BOOL initial) {
    char nb[160];
    *h = CreateEventA(NULL, type == NotificationEvent, initial, oa_name(xoa, nb));
    return *h ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
NTSTATUS WINAPI xb_NtCreateMutant(HANDLE* h, XOBJECT_ATTRIBUTES* xoa, BOOL owner) {
    char nb[160];
    *h = CreateMutexA(NULL, owner, oa_name(xoa, nb));
    return *h ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
NTSTATUS WINAPI xb_NtCreateSemaphore(HANDLE* h, XOBJECT_ATTRIBUTES* xoa, s32 initial, s32 max) {
    char nb[160];
    *h = CreateSemaphoreA(NULL, initial, max, oa_name(xoa, nb));
    return *h ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}
NTSTATUS WINAPI xb_NtCreateTimer(HANDLE* h, XOBJECT_ATTRIBUTES* xoa, u32 type) {
    (void)xoa; return NtCreateTimer(h, 0x1F0003 /* TIMER_ALL_ACCESS */, NULL, type);
}
NTSTATUS WINAPI xb_NtSetTimerEx(HANDLE h, s64* due, void* apc, s8 mode, void* ctx, BOOL resume, s32 period, BOOL* prev) {
    (void)mode; return NtSetTimer(h, due, apc, ctx, resume, period, prev);
}
NTSTATUS WINAPI xb_NtSetEvent(HANDLE h, s32* prev) { return NtSetEvent(h, prev); }
NTSTATUS WINAPI xb_NtReleaseMutant(HANDLE h, s32* prev) { return NtReleaseMutant(h, prev); }
NTSTATUS WINAPI xb_NtReleaseSemaphore(HANDLE h, s32 n, s32* prev) { return NtReleaseSemaphore(h, n, prev); }
NTSTATUS WINAPI xb_NtWaitForSingleObject(HANDLE h, BOOL alertable, s64* t) { return NtWaitForSingleObject(h, alertable, t); }
NTSTATUS WINAPI xb_NtWaitForSingleObjectEx(HANDLE h, s8 mode, BOOL alertable, s64* t) { (void)mode; return NtWaitForSingleObject(h, alertable, t); }
NTSTATUS WINAPI xb_NtWaitForMultipleObjectsEx(u32 n, HANDLE* hs, u32 type, s8 mode, BOOL alertable, s64* t) {
    (void)mode; return NtWaitForMultipleObjects(n, hs, type, alertable, t);
}
NTSTATUS WINAPI xb_NtDuplicateObject(HANDLE src, HANDLE* dst, u32 options) {
    return DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(), dst, 0, FALSE, options | DUPLICATE_SAME_ACCESS)
        ? STATUS_SUCCESS : STATUS_INVALID_HANDLE;
}
NTSTATUS WINAPI xb_NtYieldExecution(void) { SwitchToThread(); return STATUS_SUCCESS; }
NTSTATUS WINAPI xb_NtResumeThread(HANDLE h, u32* prev) { u32 r = ResumeThread(h); if (prev) *prev = r; return STATUS_SUCCESS; }
NTSTATUS WINAPI xb_NtSuspendThread(HANDLE h, u32* prev) { u32 r = SuspendThread(h); if (prev) *prev = r; return STATUS_SUCCESS; }

NTSTATUS WINAPI xb_NtAllocateVirtualMemory(void** base, u32 zeroBits, u32* size, u32 type, u32 protect) {
    type &= ~0x800000u; /* MEM_NOZERO */
    NTSTATUS s = NtAllocateVirtualMemory(GetCurrentProcess(), base, zeroBits, size, type, host_protect(protect));
    TRACE("NtAllocateVirtualMemory(%p, %x, type=%x) -> %x\n", TID, *base, *size, type, s);
    return s;
}
NTSTATUS WINAPI xb_NtFreeVirtualMemory(void** base, u32* size, u32 type) {
    return NtFreeVirtualMemory(GetCurrentProcess(), base, size, type);
}
NTSTATUS WINAPI xb_NtQueryVirtualMemory(void* base, MEMORY_BASIC_INFORMATION* mbi) {
    return VirtualQuery(base, mbi, sizeof(*mbi)) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

/* Async I/O completion trampoline used by XAPI ReadFileEx/WriteFileEx */
void WINAPI xb_NtUserIoApcDispatcher(void* ctx, IO_STATUS_BLOCK* iosb, u32 reserved) {
    (void)reserved;
    u32 err = 0, n = 0;
    if (NT_SUCCESS(iosb->Status)) n = iosb->Information; else err = RtlNtStatusToDosError(iosb->Status);
    ((void (WINAPI*)(u32, u32, void*))ctx)(err, n, iosb); /* OVERLAPPED starts with the IOSB */
}

NTSTATUS WINAPI xb_ObReferenceObjectByHandle(HANDLE h, void* type, void** obj) {
    if (type == xb_PsThreadObjectType && (h == (HANDLE)-2 || !h)) { *obj = cur_xthread()->Thread; return STATUS_SUCCESS; }
    /* Give callers a private dispatcher-sized blob so they don't fault */
    static u8 dummy[0x40];
    TRACE("ObReferenceObjectByHandle(%p, type=%p)\n", TID, h, type);
    *obj = dummy;
    return STATUS_SUCCESS;
}
void FASTCALL xb_ObfDereferenceObject(void* obj) { (void)obj; }

/* XBK_SAMPLE=1: print where every game thread is every 2 s (hang hunting) */
#define MAX_GAME_THREADS 32
static HANDLE g_gameThreads[MAX_GAME_THREADS];
static u32    g_gameTids[MAX_GAME_THREADS];
static int    g_nGameThreads;

static u32 WINAPI sampler_thread(void* arg) {
    (void)arg;
    for (;;) {
        Sleep(2000);
        for (int i = 0; i < g_nGameThreads; i++) {
            u8 ctx[0x2CC] __attribute__((aligned(16)));
            memset(ctx, 0, sizeof(ctx));
            *(u32*)ctx = 0x10001; /* CONTEXT_i386 | CONTEXT_CONTROL */
            SuspendThread(g_gameThreads[i]);
            if (GetThreadContext(g_gameThreads[i], ctx)) {
                u32 esp = *(u32*)(ctx + 0xC4);
                u32* st = (u32*)esp;
                klog("[sample] tid %x eip=%p esp=%p  stack: %p %p %p %p %p %p\n", g_gameTids[i],
                     *(u32*)(ctx + 0xB8), esp, st[0], st[1], st[2], st[3], st[4], st[5]);
            }
            ResumeThread(g_gameThreads[i]);
        }
    }
    return 0;
}

typedef struct {
    void (WINAPI* system)(void*, void*);
    void* start; void* ctx; u32 tlsSize; HANDLE ready;
} THREAD_START;

static u32 WINAPI xbox_thread_entry(void* p) {
    THREAD_START ts = *(THREAD_START*)p;
    HeapFree(g_heap, 0, p);
    thread_setup(ts.tlsSize);
    TRACE("thread start routine=%p ctx=%p tls=%x\n", TID, ts.start, ts.ctx, ts.tlsSize);
    if (ts.system) ts.system(ts.start, ts.ctx);
    else ((void (WINAPI*)(void*))ts.start)(ts.ctx);
    return 0;
}

NTSTATUS WINAPI xb_PsCreateSystemThreadEx(HANDLE* h, u32 extSize, u32 stackSize, u32 tlsSize, u32* tid,
                                          void* start, void* ctx, BOOL suspended, BOOL debugger, void* system) {
    (void)extSize; (void)debugger;
    THREAD_START* ts = HeapAlloc(g_heap, 0, sizeof(*ts));
    ts->system = system; ts->start = start; ts->ctx = ctx; ts->tlsSize = tlsSize;
    u32 id;
    if (stackSize < 0x40000) stackSize = 0x40000;
    HANDLE th = CreateThread(NULL, stackSize, xbox_thread_entry, ts, suspended ? CREATE_SUSPENDED : 0, &id);
    klog("[krnl] PsCreateSystemThreadEx(start=%p ctx=%p tls=%x) -> tid %x\n", start, ctx, tlsSize, id);
    if (!th) return STATUS_NO_MEMORY;
    if (g_nGameThreads < MAX_GAME_THREADS) {
        DuplicateHandle(GetCurrentProcess(), th, GetCurrentProcess(), &g_gameThreads[g_nGameThreads], 0, FALSE, DUPLICATE_SAME_ACCESS);
        g_gameTids[g_nGameThreads++] = id;
    }
    if (h) *h = th; else CloseHandle(th);
    if (tid) *tid = id;
    return STATUS_SUCCESS;
}
void WINAPI xb_PsTerminateSystemThread(NTSTATUS status) { TRACE("PsTerminateSystemThread(%x)\n", TID, status); ExitThread(status); }

/* ------------------------------------------------------------------ */
/* Rtl*                                                                */
/* ------------------------------------------------------------------ */

NTSTATUS WINAPI xb_RtlAnsiStringToUnicodeString(UNICODE_STRING* d, ANSI_STRING* s, BOOL alloc) {
    u32 n = s->Length;
    if (alloc) { d->Buffer = HeapAlloc(g_heap, 0, n * 2 + 2); d->MaximumLength = (u16)(n * 2 + 2); }
    else if (d->MaximumLength < n * 2) return STATUS_BUFFER_TOO_SMALL;
    for (u32 i = 0; i < n; i++) d->Buffer[i] = (u8)s->Buffer[i];
    if (d->MaximumLength >= n * 2 + 2) d->Buffer[n] = 0;
    d->Length = (u16)(n * 2);
    return STATUS_SUCCESS;
}
u32 WINAPI xb_RtlCompareMemoryUlong(void* src, u32 len, u32 pattern) {
    u32* p = src; u32 n = 0;
    while (n + 4 <= len && *p == pattern) { p++; n += 4; }
    return n;
}
BOOL WINAPI xb_RtlEqualString(ANSI_STRING* a, ANSI_STRING* b, BOOL ci) {
    if (a->Length != b->Length) return FALSE;
    for (u32 i = 0; i < a->Length; i++) {
        char x = a->Buffer[i], y = b->Buffer[i];
        if (ci) { x = xlower(x); y = xlower(y); }
        if (x != y) return FALSE;
    }
    return TRUE;
}
void WINAPI xb_RtlInitAnsiString(ANSI_STRING* d, const char* s) {
    d->Buffer = (char*)s;
    if (s) { u32 l = xstrlen(s); d->Length = (u16)l; d->MaximumLength = (u16)(l + 1); }
    else d->Length = d->MaximumLength = 0;
}

/* Xbox RTL_CRITICAL_SECTION: { DISPATCHER_HEADER Event; LONG LockCount; LONG RecursionCount; HANDLE OwningThread } */
typedef struct { DISPATCHER_HEADER Event; volatile s32 LockCount; s32 RecursionCount; u32 OwningThread; } XCRITICAL_SECTION;

void WINAPI xb_RtlInitializeCriticalSection(XCRITICAL_SECTION* cs) {
    memset(cs, 0, sizeof(*cs));
    cs->Event.Type = SynchronizationEvent;
    cs->Event.Size = 4;
    cs->LockCount = -1;
}
void WINAPI xb_RtlEnterCriticalSection(XCRITICAL_SECTION* cs) {
    u32 me = TID;
    if (InterlockedIncrement(&cs->LockCount) == 0) {
        cs->OwningThread = me; cs->RecursionCount = 1;
    } else if (cs->OwningThread == me) {
        cs->RecursionCount++;
    } else {
        disp_wait(&cs->Event, NULL);
        cs->OwningThread = me; cs->RecursionCount = 1;
    }
}
void WINAPI xb_RtlLeaveCriticalSection(XCRITICAL_SECTION* cs) {
    if (--cs->RecursionCount) { InterlockedDecrement(&cs->LockCount); return; }
    cs->OwningThread = 0;
    if (InterlockedDecrement(&cs->LockCount) >= 0) disp_signal(&cs->Event, 1);
}
u32  WINAPI xb_RtlNtStatusToDosError(NTSTATUS s) { return RtlNtStatusToDosError(s); }
void WINAPI xb_RtlRaiseException(void* rec) { RtlRaiseException(rec); }
void WINAPI xb_RtlUnwind(void* frame, void* ip, void* rec, void* ret) { RtlUnwind(frame, ip, rec, ret); }
BOOL WINAPI xb_RtlTimeFieldsToTime(void* tf, s64* t) { return RtlTimeFieldsToTime(tf, t); }
void WINAPI xb_RtlTimeToTimeFields(s64* t, void* tf) { RtlTimeToTimeFields(t, tf); }

/* ------------------------------------------------------------------ */
/* Xe* - sections are all mapped by the flat PE already                */
/* ------------------------------------------------------------------ */

typedef struct { u32 Flags, VirtualAddress, VirtualSize, FileAddress, FileSize; char* Name; s32 RefCount; } XBE_SECTION;
NTSTATUS WINAPI xb_XeLoadSection(XBE_SECTION* s) { s->RefCount++; TRACE("XeLoadSection(%s)\n", TID, s->Name); return STATUS_SUCCESS; }
NTSTATUS WINAPI xb_XeUnloadSection(XBE_SECTION* s) { if (s->RefCount > 0) s->RefCount--; return STATUS_SUCCESS; }

/* ------------------------------------------------------------------ */
/* Xc* - SHA-1 / HMAC (save game signatures)                           */
/* ------------------------------------------------------------------ */

typedef struct { u32 h[5]; u64 len; u8 buf[64]; u32 n; } SHA1_CTX;
#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
static void sha1_block(SHA1_CTX* c, const u8* p) {
    u32 w[80], a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
    for (int i = 0; i < 16; i++) w[i] = (u32)p[i*4] << 24 | (u32)p[i*4+1] << 16 | (u32)p[i*4+2] << 8 | p[i*4+3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    for (int i = 0; i < 80; i++) {
        u32 k, fn;
        if (i < 20)      { fn = (b & d) | (~b & e);          k = 0x5A827999; }
        else if (i < 40) { fn = b ^ d ^ e;                    k = 0x6ED9EBA1; }
        else if (i < 60) { fn = (b & d) | (b & e) | (d & e);  k = 0x8F1BBCDC; }
        else             { fn = b ^ d ^ e;                    k = 0xCA62C1D6; }
        u32 t = ROL(a, 5) + fn + f + k + w[i];
        f = e; e = d; d = ROL(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f;
}
void WINAPI xb_XcSHAInit(SHA1_CTX* c) {
    c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE; c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
    c->len = 0; c->n = 0;
}
void WINAPI xb_XcSHAUpdate(SHA1_CTX* c, const u8* p, u32 n) {
    c->len += n;
    while (n--) { c->buf[c->n++] = *p++; if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; } }
}
void WINAPI xb_XcSHAFinal(SHA1_CTX* c, u8* out) {
    u64 bits = c->len * 8; u8 pad = 0x80;
    xb_XcSHAUpdate(c, &pad, 1); pad = 0;
    while (c->n != 56) xb_XcSHAUpdate(c, &pad, 1);
    u8 l[8]; for (int i = 0; i < 8; i++) l[i] = (u8)(bits >> (56 - i * 8));
    xb_XcSHAUpdate(c, l, 8);
    for (int i = 0; i < 20; i++) out[i] = (u8)(c->h[i / 4] >> (24 - (i % 4) * 8));
}
void WINAPI xb_XcHMAC(const u8* key, u32 klen, const u8* d1, u32 l1, const u8* d2, u32 l2, u8* out) {
    u8 k[64], t[20]; SHA1_CTX c;
    memset(k, 0, 64);
    if (klen > 64) { xb_XcSHAInit(&c); xb_XcSHAUpdate(&c, key, klen); xb_XcSHAFinal(&c, k); } else memcpy(k, key, klen);
    for (int i = 0; i < 64; i++) k[i] ^= 0x36;
    xb_XcSHAInit(&c); xb_XcSHAUpdate(&c, k, 64);
    if (d1) xb_XcSHAUpdate(&c, d1, l1);
    if (d2) xb_XcSHAUpdate(&c, d2, l2);
    xb_XcSHAFinal(&c, t);
    for (int i = 0; i < 64; i++) k[i] ^= 0x36 ^ 0x5C;
    xb_XcSHAInit(&c); xb_XcSHAUpdate(&c, k, 64); xb_XcSHAUpdate(&c, t, 20); xb_XcSHAFinal(&c, out);
}

/* ------------------------------------------------------------------ */
/* Process bring-up                                                    */
/* ------------------------------------------------------------------ */

/* The original XBE image header page, written here by build_flat_pe.py.
 * The PE loader needed 0x10000 for MZ/PE headers; XAPI reads certificate,
 * library versions and section headers from it, so put it back at entry. */
u8  g_xbeHeader[0x1000] = { 'X', 'B', 'E', 'H' };
u32 g_xbeEntry = 0;

static void restore_xbe_header(void) {
    u8* hdr = (u8*)0x10000;
    u32 old;
    VirtualProtect(hdr, 0x1000, PAGE_EXECUTE_READWRITE, &old);
    memcpy(hdr, g_xbeHeader, sizeof(g_xbeHeader));
}

/* Some XDK code walks the kernel image header at 0x80010000. Give it a
 * minimal PE with a single non-INIT section (needs LARGEADDRESSAWARE). */
static void map_fake_kernel_image(void) {
    u8* k = (u8*)0x80010000; /* inside the physical RAM window */
    k[0] = 'M'; k[1] = 'Z';
    *(u32*)(k + 0x3C) = 0x80;
    memcpy(k + 0x80, "PE\0\0", 4);
    *(u16*)(k + 0x80 + 0x04) = 0x14C;
    *(u16*)(k + 0x80 + 0x06) = 1;     /* NumberOfSections      */
    *(u16*)(k + 0x80 + 0x14) = 0xE0;  /* SizeOfOptionalHeader  */
    memcpy(k + 0x80 + 0x18 + 0xE0, ".text\0\0\0", 8);
}

/* Physical MMIO windows the XDK libraries poke directly: NV2A (0xFD000000,
 * 16 MB), APU / AC97 / USB (0xFE800000..0xFF000000). Backed by plain memory
 * so register writes land somewhere; GPU behaviour is emulated on top. */
static void map_mmio(void) {
    void* ram = VirtualAlloc((void*)PHYS_BASE, PHYS_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    klog("[krnl] physical RAM window 0x80000000-0x84000000 %s\n", ram ? "mapped" : "NOT mapped");
    void* p = VirtualAlloc((void*)0xFD000000, 0x02000000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    klog("[krnl] MMIO window 0xFD000000-0xFF000000 %s\n", p ? "mapped" : "NOT mapped");
}

/* Ring-0 instructions the XDK executes (port I/O to SMBus/ACPI/PCI config,
 * cli/sti, wbinvd) fault with PRIV_INSTRUCTION in user mode. Emulate them
 * here: writes are dropped, reads return 0. */
#define CTX_EDX 0xA8
#define CTX_EAX 0xB0
#define CTX_EIP 0xB8

static u32 port_read(u16 port, int size) {
    static u16 seen[64]; static int nseen;
    int known = 0;
    for (int i = 0; i < nseen; i++) if (seen[i] == port) known = 1;
    if (!known && nseen < 64) { seen[nseen++] = port; klog("[krnl] in  port %x (size %d) -> 0\n", port, size); }
    return 0;
}
static void port_write(u16 port, u32 v, int size) {
    static u16 seen[64]; static int nseen;
    int known = 0;
    for (int i = 0; i < nseen; i++) if (seen[i] == port) known = 1;
    if (!known && nseen < 64) { seen[nseen++] = port; klog("[krnl] out port %x <- %x (size %d)\n", port, v, size); }
}

static s32 WINAPI priv_handler(void* ep) {
    u32* rec = *(u32**)ep;
    u8*  ctx = ((u8**)ep)[1];
    if (rec[0] != 0xC0000096 /* PRIV_INSTRUCTION */) return 0;
    u32* eip = (u32*)(ctx + CTX_EIP);
    u32* eax = (u32*)(ctx + CTX_EAX);
    u16  dx  = (u16)*(u32*)(ctx + CTX_EDX);
    u8* ip = (u8*)*eip; u32 len = 0; int opsz = 4;
    if (ip[0] == 0x66) { opsz = 2; len = 1; }
    u8 op = ip[len];
    switch (op) {
    case 0xE4: case 0xE5: { int sz = op == 0xE4 ? 1 : opsz; u32 v = port_read(ip[len + 1], sz);
        *eax = sz == 1 ? (*eax & ~0xFFu) | (v & 0xFF) : sz == 2 ? (*eax & ~0xFFFFu) | (v & 0xFFFF) : v; len += 2; break; }
    case 0xEC: case 0xED: { int sz = op == 0xEC ? 1 : opsz; u32 v = port_read(dx, sz);
        *eax = sz == 1 ? (*eax & ~0xFFu) | (v & 0xFF) : sz == 2 ? (*eax & ~0xFFFFu) | (v & 0xFFFF) : v; len += 1; break; }
    case 0xE6: case 0xE7: port_write(ip[len + 1], *eax, op == 0xE6 ? 1 : opsz); len += 2; break;
    case 0xEE: case 0xEF: port_write(dx, *eax, op == 0xEE ? 1 : opsz); len += 1; break;
    case 0xFA: case 0xFB: case 0xF4: len += 1; break;                  /* cli / sti / hlt */
    case 0x0F:
        if (ip[len + 1] == 0x09 || ip[len + 1] == 0x08) { len += 2; break; } /* wbinvd / invd */
        if (ip[len + 1] == 0x30) { len += 2; break; }                      /* wrmsr */
        if (ip[len + 1] == 0x32) { *eax = 0; *(u32*)(ctx + CTX_EDX) = 0; len += 2; break; } /* rdmsr */
        /* fallthrough */
    default:
        klog("[krnl] unhandled privileged instruction %x %x at %p\n", ip[0], ip[1], ip);
        return 0;
    }
    *eip += len;
    return -1; /* EXCEPTION_CONTINUE_EXECUTION */
}

static void init_paths(void) {
    char exe[260];
    u32 n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    while (n && exe[n - 1] != '\\') n--;
    if (n) n--;
    memcpy(g_gameDir, exe, n); g_gameDir[n] = 0;
    memcpy(g_saveDir, g_gameDir, n); memcpy(g_saveDir + n, "\\_hdd", 6);
    CreateDirectoryA(g_saveDir, NULL);
    static const char* subs[] = { "\\E", "\\C", "\\X", "\\Y", "\\Z", "\\F" };
    for (u32 i = 0; i < 6; i++) {
        char p[280]; u32 l = xstrlen(g_saveDir);
        memcpy(p, g_saveDir, l); memcpy(p + l, subs[i], 3);
        CreateDirectoryA(p, NULL);
    }
    /* D: is the DVD (game folder) even before XAPI creates the link */
    link_add("\\??\\D:", 6, "\\Device\\CdRom0", 14);
    link_add("\\??\\CdRom0:", 11, "\\Device\\CdRom0", 14);

    char logp[300]; u32 l = xstrlen(g_gameDir);
    memcpy(logp, g_gameDir, l); memcpy(logp + l, "\\xboxkrnl.log", 14);
    g_log = CreateFileA(logp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (g_log == INVALID_HANDLE_VALUE) g_log = NULL;
}

/* PE entry point of the patched exe. Wine has finished with the PE headers
 * by now, so the XBE header can go back to 0x10000 before XAPI runs. */
void xb_ExeEntry(void) {
    g_heap = GetProcessHeap();
    InitializeCriticalSection(&g_logLock);
    InitializeCriticalSection(&g_dispLock);
    InitializeCriticalSection(&g_dpcLock);
    InitializeCriticalSection(&g_rawLock);
    InitializeCriticalSection(&g_physLock);
    InitializeConditionVariable(&g_dispCond);
    QueryPerformanceFrequency(&g_qpcFreq);
    char e[8];
    g_trace = GetEnvironmentVariableA("XBK_TRACE", e, sizeof(e)) > 0;
    if (GetEnvironmentVariableA("XBK_SAMPLE", e, sizeof(e)) > 0)
        CreateThread(NULL, 0x10000, sampler_thread, NULL, 0, NULL);
    init_paths();
    klog("[krnl] kernel up. game dir: %s\n", g_gameDir);
    map_mmio();
    map_fake_kernel_image();
    AddVectoredExceptionHandler(1, priv_handler);
    restore_xbe_header();
    thread_setup(0);
    CreateThread(NULL, 0x10000, interrupt_thread, NULL, 0, NULL);

    klog("[krnl] jumping to XBE entry %p\n", g_xbeEntry);
    ((void (*)(void))g_xbeEntry)();
    /* Like the real kernel: the boot thread ends, the game threads live on */
    ExitThread(0);
}
