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

