/*
 * PC audio for default.xbe: replaces the Xbox XACT engine and DirectSound
 * (which drive the MCPX APU directly).
 *
 * Stage 1: silent. Every call the game makes succeeds and hands out dummy
 * objects, so the game runs; playback of the original .xwb / .xsb banks
 * goes here next. Signatures are the XDK 5849 C wrappers (pThis first).
 */
#include "win32.h"

static u32 g_dummy_objects[64][16];
static u32 g_next_dummy;

static void xlog(const char* n, u32 a, u32 b, u32 c, u32 d);
static void* dummy(void)
{
    void* p = g_dummy_objects[g_next_dummy];
    g_next_dummy = (g_next_dummy + 1) & 63;
    return p;
}

#define S_OK 0
#define E_FAIL ((HRESULT)0x80004005)

/* XACTEngineCreate(pParams, ppEngine) */
HRESULT WINAPI pc_XACTEngineCreate(const void* params, void** pp) { *pp = dummy(); return S_OK; }
void    WINAPI pc_XACTEngineDoWork(void) { }

/* IXACTEngine */
HRESULT WINAPI pc_XACT_CreateSoundSource(void* e, u32 flags, void** pp) { *pp = dummy(); xlog("[x] CreateSoundSource", flags, (u32)*pp, 0, 0); return S_OK; }
HRESULT WINAPI pc_XACT_CreateSoundBank(void* e, const void* buf, u32 size, void** pp) { *pp = dummy(); xlog("[x] CreateSoundBank", (u32)buf, size, (u32)*pp, 0); log_str((const char*)buf + 0x28); log_str("\n"); return S_OK; }
/* Wave banks are "prepared" at once: each registration queues the
 * WAVEBANKPREPARED notification the game waits for before it goes on
 * (sound init thread, 0x1acf31). */
#define XACTNOTIFICATIONTYPE_WAVEBANKPREPARED 3
static void* g_prepared[16];
static u32   g_prepared_head, g_prepared_tail;

static void* wave_bank(void)
{
    void* wb = dummy();
    g_prepared[g_prepared_tail++ & 15] = wb;
    return wb;
}

HRESULT WINAPI pc_XACT_RegisterWaveBank(void* e, const void* data, u32 size, void** pp) { *pp = wave_bank(); xlog("[x] RegisterWaveBank", (u32)data, size, (u32)*pp, 0); log_str((const char*)data + 0x30); log_str("\n"); return S_OK; }
HRESULT WINAPI pc_XACT_RegisterStreamedWaveBank(void* e, const u32* params, void** pp) { *pp = wave_bank(); xlog("[x] RegisterStreamedWaveBank", params[0], params[1], params[2], params[3]); xlog("[x]   +", params[4], params[5], (u32)*pp, 0); return S_OK; }

/* GetNotification(pEngine, pNotificationDesc, pNotification): +0 WORD type, +4 pWaveBank */
HRESULT WINAPI pc_XACT_GetNotification(void* e, const void* desc, void* out)
{
    if (g_prepared_head == g_prepared_tail) return E_FAIL;           /* none pending */
    u8* n = out;
    *(u16*)n = XACTNOTIFICATIONTYPE_WAVEBANKPREPARED; *(u16*)(n + 2) = 0;
    *(void**)(n + 4) = g_prepared[g_prepared_head++ & 15];
    return S_OK;
}
HRESULT WINAPI pc_XACT_Out2(void* e, u32* out) { if (out) *out = 0; return S_OK; }
HRESULT WINAPI pc_XACT_GetCueIndex(void* sb, const char* name, u32* index) { *index = 0; xlog("[x] GetCueIndex", (u32)sb, (u32)index, 0, 0); log_str("[x]   name "); log_str(name); log_str("\n"); return S_OK; }

/* calls with nothing to hand back, by argument count */
HRESULT WINAPI pc_XACT_Nop1(void* a) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop2(void* a, void* b) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop3(void* a, void* b, void* c) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop4(void* a, void* b, void* c, void* d) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop5(void* a, void* b, void* c, void* d, void* e) { return S_OK; }

/* call log while the semantics are worked out */
static u32 g_xlog;
static void xlog(const char* n, u32 a, u32 b, u32 c, u32 d)
{
    if (g_xlog++ > 4000) return;
    char t[128]; int i = 0;
    while (*n && i < 60) t[i++] = *n++;
    u32 v[4] = { a, b, c, d };
    for (int k = 0; k < 4; k++) {
        t[i++] = ' ';
        for (int j = 28; j >= 0; j -= 4) t[i++] = "0123456789abcdef"[(v[k] >> j) & 15];
    }
    t[i++] = '\n'; t[i] = 0;
    log_str(t);
}
HRESULT WINAPI pc_XACT_Eng3(u32 e, u32 a, u32 b) { xlog("[x] Eng3", e, a, b, 0); return S_OK; }
HRESULT WINAPI pc_XACT_Eng2(u32 e, u32 a) { xlog("[x] Eng2", e, a, 0, 0); return S_OK; }
HRESULT WINAPI pc_XACT_Release(u32 o) { xlog("[x] Release", o, 0, 0, 0); return S_OK; }
HRESULT WINAPI pc_XACT_SB_e00(u32 sb, u32 a, u32 b) { xlog("[x] SB_e00", sb, a, b, 0); return S_OK; }
HRESULT WINAPI pc_XACT_SB_PlayEx(u32 sb, u32* p, u32* out)
{
    xlog("[x] SB_PlayEx", sb, p[0], p[1], p[2]); xlog("[x]   +c", p[3], p[4], p[5], (u32)out);
    if (out) *out = (u32)dummy();
    return S_OK;
}
HRESULT WINAPI pc_XACT_SB_e66(u32 sb, u32 a, u32 b, u32* c) { xlog("[x] SB_e66", sb, a, b, (u32)c); if (c) *c = (u32)dummy(); return S_OK; }
HRESULT WINAPI pc_XACT_SB_e9d(u32 sb, u32 a, u32 b) { xlog("[x] SB_e9d", sb, a, b, 0); return S_OK; }
HRESULT WINAPI pc_XACT_Cue_ed0(u32 c) { xlog("[x] Cue_ed0", c, 0, 0, 0); return S_OK; }
HRESULT WINAPI pc_XACT_Cue_ed5(u32 c, float x, float y, float z, u32 f) { return S_OK; }
HRESULT WINAPI pc_XACT_Cue_efe(u32 c, u32 a) { xlog("[x] Cue_efe", c, a, 0, 0); return S_OK; }
HRESULT WINAPI pc_XACT_Cue_f03(u32 c, u32 a) { xlog("[x] Cue_f03", c, a, 0, 0); return S_OK; }
HRESULT WINAPI pc_XACT_Cue_f08(u32 c) { xlog("[x] Cue_f08", c, 0, 0, 0); return S_OK; }
HRESULT WINAPI pc_XACT_Cue_f0d(u32 c, u32* o) { xlog("[x] Cue_f0d", c, 0, 0, 0); if (o) *o = 0; return S_OK; }

/* DirectSound */
void    WINAPI pc_DirectSoundUseLightHRTF4Channel(void) { }
/* XAudioDownloadEffectsImage(pszImageName, pImageLoc, dwFlags, ppImageDesc) */
HRESULT WINAPI pc_XAudioDownloadEffectsImage(const char* name, void* loc, u32 flags, void** pp)
{
    if (pp) *pp = dummy();
    return S_OK;
}
