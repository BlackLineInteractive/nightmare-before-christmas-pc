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
HRESULT WINAPI pc_XACT_CreateSoundSource(void* e, u32 flags, void** pp) { *pp = dummy(); return S_OK; }
HRESULT WINAPI pc_XACT_CreateSoundBank(void* e, const void* buf, u32 size, void** pp) { *pp = dummy(); return S_OK; }
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

HRESULT WINAPI pc_XACT_RegisterWaveBank(void* e, const void* data, u32 size, void** pp) { *pp = wave_bank(); return S_OK; }
HRESULT WINAPI pc_XACT_RegisterStreamedWaveBank(void* e, const void* params, void** pp) { *pp = wave_bank(); return S_OK; }

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
HRESULT WINAPI pc_XACT_GetCueIndex(void* sb, const char* name, u32* index) { *index = 0; return S_OK; }

/* calls with nothing to hand back, by argument count */
HRESULT WINAPI pc_XACT_Nop1(void* a) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop2(void* a, void* b) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop3(void* a, void* b, void* c) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop4(void* a, void* b, void* c, void* d) { return S_OK; }
HRESULT WINAPI pc_XACT_Nop5(void* a, void* b, void* c, void* d, void* e) { return S_OK; }

/* DirectSound */
void    WINAPI pc_DirectSoundUseLightHRTF4Channel(void) { }
/* XAudioDownloadEffectsImage(pszImageName, pImageLoc, dwFlags, ppImageDesc) */
HRESULT WINAPI pc_XAudioDownloadEffectsImage(const char* name, void* loc, u32 flags, void** pp)
{
    if (pp) *pp = dummy();
    return S_OK;
}
