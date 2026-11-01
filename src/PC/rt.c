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


/* ------------------------------------------------------------- watchdog */

/* Every few seconds report where the game's main thread is, so stalls are
 * visible in the log. Started once by the renderer. */
IMPORT HANDLE WINAPI CreateThread(void* sa, size_t stack, u32 (WINAPI *fn)(void*), void* arg, u32 flags, u32* tid);
IMPORT HANDLE WINAPI OpenThread(u32 access, BOOL inherit, u32 tid);
IMPORT u32    WINAPI GetCurrentThreadId(void);
IMPORT u32    WINAPI SuspendThread(HANDLE h);
IMPORT u32    WINAPI ResumeThread(HANDLE h);
IMPORT BOOL   WINAPI GetThreadContext(HANDLE h, void* ctx);

static HANDLE g_main_thread;

static u32 WINAPI watchdog(void* arg)
{
    static u32 ctx[0x2CC / 4];
    for (;;) {
        Sleep(5000);
        SuspendThread(g_main_thread);
        ctx[0] = 0x10001;                       /* CONTEXT_CONTROL */
        BOOL ok = GetThreadContext(g_main_thread, ctx);
        ResumeThread(g_main_thread);
        if (!ok) continue;
        u32 eip = ctx[0xB8 / 4], esp = ctx[0xC4 / 4];
        log_hex("[pc] watchdog main thread eip", eip);
        u32* sp = (u32*)esp;
        int shown = 0;
        for (int i = 0; i < 512 && shown < 8; i++) {
            u32 r = sp[i];
            if (r < 0x12006 || r >= 0x431000) continue;
            const u8* c = (const u8*)r;
            if (c[-5] == 0xE8 || (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10) || (c[-2] == 0xFF && (c[-1] & 0x38) == 0x10)) {
                log_hex("[pc]   called from", r);
                shown++;
            }
        }
    }
}

static void start_watchdog(void)
{
    g_main_thread = OpenThread(0x1FFFFF, 0, GetCurrentThreadId());
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
}
