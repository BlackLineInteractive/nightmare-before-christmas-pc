/*
 * Xbox "physical" (contiguous) memory for default.xbe.
 *
 * Xbox code keeps physical addresses in its data structures (D3D resource
 * Data fields, XPR offsets) and turns them back into pointers with
 * PA | 0x80000000 (cached) or PA | 0xF0000000 (write-combined). So the 64 MB
 * pool is one block of memory mapped at both of those addresses, and every
 * contiguous allocation (XPhysicalAlloc, physical XMemAlloc, D3D resources)
 * comes from it. The exe is LARGE_ADDRESS_AWARE, so both ranges are free
 * for a 32-bit process on 64-bit Windows.
 */
#include "win32.h"

IMPORT HANDLE WINAPI CreateFileMappingA(HANDLE f, void* sa, u32 prot, u32 hi, u32 lo, const char* name);
IMPORT void*  WINAPI MapViewOfFileEx(HANDLE m, u32 access, u32 hi, u32 lo, size_t n, void* at);
IMPORT HANDLE WINAPI GetProcessHeap(void);
IMPORT void*  WINAPI HeapAlloc(HANDLE h, u32 flags, size_t n);
IMPORT BOOL   WINAPI HeapFree(HANDLE h, u32 flags, void* p);

#define PHYS_SIZE   0x04000000u          /* 64 MB, as on the console */
#define PHYS_CACHED 0x80000000u
#define PHYS_WC     0xF0000000u
#define PAGE        0x1000u
#define NPAGES      (PHYS_SIZE / PAGE)

static int g_phys_ready;
static u32 g_run[NPAGES];               /* pages in the allocation starting here, 0 = free */
static u8  g_used[NPAGES];

static void phys_init(void)
{
    if (g_phys_ready) return;
    g_phys_ready = 1;
    HANDLE m = CreateFileMappingA((HANDLE)-1, NULL, PAGE_READWRITE, 0, PHYS_SIZE, NULL);
    void* a = MapViewOfFileEx(m, 0xF001F, 0, 0, PHYS_SIZE, (void*)PHYS_CACHED);
    void* b = MapViewOfFileEx(m, 0xF001F, 0, 0, PHYS_SIZE, (void*)PHYS_WC);
    if (a != (void*)PHYS_CACHED || b != (void*)PHYS_WC) {
        log_str("[pc] cannot map Xbox physical memory at 0x80000000 / 0xF0000000\n");
        ExitProcess(1);
    }
    g_used[0] = 1; g_run[0] = 1;        /* keep physical address 0 unused */
}

/* -> pointer in the 0x80000000 view, NULL if the pool is exhausted */
static void* phys_alloc(u32 size, u32 align)
{
    phys_init();
    u32 n = (size + PAGE - 1) / PAGE, step = align > PAGE ? align / PAGE : 1;
    if (!n) n = 1;
    for (u32 p = 0; p + n <= NPAGES; p += step) {
        u32 k = 0;
        while (k < n && !g_used[p + k]) k++;
        if (k == n) {
            for (k = 0; k < n; k++) g_used[p + k] = 1;
            g_run[p] = n;
            u8* ptr = (u8*)(PHYS_CACHED + p * PAGE);
            memset(ptr, 0, n * PAGE);
            return ptr;
        }
        p += k / step * step;
    }
    log_hex("[pc] Xbox physical memory exhausted, size", size);
    return NULL;
}

static void phys_free(void* ptr)
{
    u32 pa = (u32)ptr & 0x0FFFFFFFu;
    if (pa >= PHYS_SIZE) return;
    u32 p = pa / PAGE, n = g_run[p];
    for (u32 k = 0; k < n; k++) g_used[p + k] = 0;
    g_run[p] = 0;
}

static int is_phys(void* p) { u32 a = (u32)p; return a >= PHYS_CACHED && a < PHYS_CACHED + PHYS_SIZE; }

/* XPhysicalAlloc(dwSize, ulPhysicalAddress, ulAlignment, flProtect) */
void* WINAPI pc_XPhysicalAlloc(u32 size, u32 maxaddr, u32 align, u32 protect) { return phys_alloc(size, align); }
/* XPhysicalAllocEx(dwSize, ulLowest, ulHighest, ulAlignment, flProtect) */
void* WINAPI pc_XPhysicalAllocEx(u32 size, u32 lo, u32 hi, u32 align, u32 protect) { return phys_alloc(size, align); }
void WINAPI pc_XPhysicalFree(void* p) { phys_free(p); }

/* XMemAlloc(dwSize, dwAllocAttributes): bit 31 = physical, bits 24..27 = alignment */
void* WINAPI pc_XMemAlloc(u32 size, u32 attr)
{
    if (attr & 0x80000000u) return phys_alloc(size, PAGE);
    return HeapAlloc(GetProcessHeap(), 8, size);
}
void WINAPI pc_XMemFree(void* p, u32 attr)
{
    if (!p) return;
    if (is_phys(p)) phys_free(p);
    else HeapFree(GetProcessHeap(), 0, p);
}
