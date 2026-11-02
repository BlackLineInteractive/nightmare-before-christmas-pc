/*
 * Freestanding C runtime bits and logging for the PC replacements (src/PC).
 */
#include "win32.h"

void* memcpy(void* d, const void* s, size_t n) { u8* a = d; const u8* b = s; while (n--) *a++ = *b++; return d; }
void* memset(void* d, int c, size_t n) { u8* a = d; while (n--) *a++ = (u8)c; return d; }

static void log_str(const char* s)
{
    u32 n = 0, w;
    while (s[n]) n++;
    WriteFile(GetStdHandle((u32)-11), s, n, &w, NULL);
}

static void log_hex(const char* what, u32 v)
{
    char b[64]; int i = 0;
    while (*what) b[i++] = *what++;
    b[i++] = ' ';
    for (int k = 7; k >= 0; k--) b[i++] = "0123456789abcdef"[(v >> (k * 4)) & 15];
    b[i++] = '\n'; b[i] = 0;
    log_str(b);
}

static void log_dec(const char* what, u32 v)
{
    char b[64]; int i = 0;
    while (*what) b[i++] = *what++;
    b[i++] = ' ';
    char d[10]; int n = 0;
    do { d[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) b[i++] = d[--n];
    b[i++] = '\n'; b[i] = 0;
    log_str(b);
}

/* microsecond timer (low 32 bits of the counter are enough between two samples) */
IMPORT BOOL WINAPI QueryPerformanceCounter(u64* c);
IMPORT BOOL WINAPI QueryPerformanceFrequency(u64* f);
static float g_us_per_tick;
static u32 ticks(void)
{
    u64 c;
    if (g_us_per_tick == 0.0f) { u64 f; QueryPerformanceFrequency(&f); g_us_per_tick = 1000000.0f / (float)(u32)f; }
    QueryPerformanceCounter(&c);
    return (u32)c;
}
static u32 us_since(u32 t0) { return (u32)((float)(ticks() - t0) * g_us_per_tick); }


/* ------------------------------------------------------------- watchdog */

/* Every few seconds report where the game's main thread is, so stalls are
 * visible in the log. Started once by the renderer. */
IMPORT HANDLE WINAPI CreateThread(void* sa, size_t stack, u32 (WINAPI *fn)(void*), void* arg, u32 flags, u32* tid);
IMPORT HANDLE WINAPI OpenThread(u32 access, BOOL inherit, u32 tid);
IMPORT u32    WINAPI GetCurrentThreadId(void);
IMPORT u32    WINAPI SuspendThread(HANDLE h);
IMPORT u32    WINAPI ResumeThread(HANDLE h);
IMPORT BOOL   WINAPI GetThreadContext(HANDLE h, void* ctx);

IMPORT HANDLE WINAPI CreateToolhelp32Snapshot(u32 flags, u32 pid);
IMPORT BOOL   WINAPI Thread32First(HANDLE snap, void* te);
IMPORT BOOL   WINAPI Thread32Next(HANDLE snap, void* te);
IMPORT u32    WINAPI GetCurrentProcessId(void);
IMPORT BOOL   WINAPI CloseHandle(HANDLE h);
IMPORT BOOL   WINAPI IsBadReadPtr(const void* p, size_t n);

static HANDLE g_main_thread;

/* EIP and the return addresses into the exe that are on a thread's stack */
static void report_thread(HANDLE t, u32 tid)
{
    static u32 ctx[0x2CC / 4];
    SuspendThread(t);
    ctx[0] = 0x10001;                       /* CONTEXT_CONTROL */
    BOOL ok = GetThreadContext(t, ctx);
    ResumeThread(t);
    if (!ok) return;
    u32 eip = ctx[0xB8 / 4], esp = ctx[0xC4 / 4];
    log_hex("[pc] watchdog thread", tid);
    log_hex("[pc]   eip", eip);
    u32* sp = (u32*)esp;
    int shown = 0, n = 1024;
    while (n && IsBadReadPtr(sp, n * 4)) n /= 2;     /* stay inside the stack */
    for (int i = 0; i < n && shown < 8; i++) {
        u32 r = sp[i];
        if (r < 0x12006 || r >= 0x431000) continue;
        const u8* c = (const u8*)r;
        if (c[-5] == 0xE8 || (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10) || (c[-2] == 0xFF && (c[-1] & 0x38) == 0x10)) {
            log_hex("[pc]   called from", r);
            shown++;
        }
    }
}

static u32 WINAPI watchdog(void* arg)
{
    u32 self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (;;) {
        Sleep(8000);
        HANDLE snap = CreateToolhelp32Snapshot(4 /* TH32CS_SNAPTHREAD */, 0);
        u32 te[7] = { 28 };                 /* THREADENTRY32: size, usage, tid, owner pid, ... */
        for (BOOL more = Thread32First(snap, te); more; more = Thread32Next(snap, te)) {
            if (te[3] != pid || te[2] == self) continue;
            HANDLE t = OpenThread(0x1FFFFF, 0, te[2]);
            if (t) { report_thread(t, te[2]); CloseHandle(t); }
        }
        CloseHandle(snap);
    }
}
static void start_watchdog(void)
{
    g_main_thread = OpenThread(0x1FFFFF, 0, GetCurrentThreadId());
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
}
