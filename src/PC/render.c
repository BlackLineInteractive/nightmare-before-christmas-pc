/*
 * PC renderer for default.xbe: replaces the Xbox D3D8 library (which drives
 * the NV2A GPU directly) with Direct3D 9 on Windows.
 *
 * tools/build_flat_pe.py links this file into default_pc.exe and patches each
 * Xbox D3D entry point the game calls into a jump to the matching pc_*
 * function below (table D3D_TO_PC in tools/pc_render.py). Signatures and
 * stack sizes follow the XDK 5849 functions they replace.
 *
 * Stage 1: device, window, Clear, Swap; resources get real memory so the
 * game can fill them. Drawing follows.
 */
#include "win32.h"
/* --------------------------------------------------------- Xbox objects */

/* Xbox D3D resource header (D3DResource / D3DPixelContainer) followed by
 * our own bookkeeping. The game reads Common/Data/Format/Size directly. */
typedef struct XRes {
    u32 Common, Data, Lock, Format, Size;
    struct XRes* Parent;           /* surfaces: owning texture */
    u32 width, height, levels, xfmt, bytes;
    u8* mem;
} XRes;

#define XCOMMON_TYPE_VERTEXBUFFER 0x00000000
#define XCOMMON_TYPE_TEXTURE      0x00040000
#define XCOMMON_TYPE_SURFACE      0x00050000

static XRes* new_res(u32 common)
{
    XRes* r = HeapAlloc(GetProcessHeap(), 8, sizeof(XRes));
    r->Common = common | 1;
    return r;
}

/* bits per pixel of an Xbox texture format (DXT: per 4x4 block = 64/128) */
static u32 xfmt_bpp(u32 f)
{
    switch (f) {
    case 0x00: case 0x01: case 0x0B: case 0x13: case 0x19: case 0x1B: case 0x1F: return 8;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x10: case 0x11: case 0x1A: case 0x1C:
    case 0x1D: case 0x20: case 0x24: case 0x25: case 0x27: case 0x28: case 0x29: case 0x2C:
    case 0x2D: case 0x30: case 0x32: case 0x35: return 16;
    case 0x0C: return 4;            /* DXT1 */
    case 0x0E: case 0x0F: return 8; /* DXT2/3, DXT4/5 */
    default: return 32;
    }
}

static int xfmt_dxt(u32 f) { return f == 0x0C || f == 0x0E || f == 0x0F; }

static u32 level_bytes(u32 f, u32 w, u32 h)
{
    if (xfmt_dxt(f)) { w = (w + 3) / 4; h = (h + 3) / 4; return w * h * (f == 0x0C ? 8 : 16); }
    return w * h * xfmt_bpp(f) / 8;
}

static u32 log2u(u32 v) { u32 n = 0; while ((1u << n) < v) n++; return n; }

/* ------------------------------------------------------------ the device */

static IUnk*  g_d3d;
static IUnk*  g_dev;
static HWND   g_hwnd;
static u32    g_width = 640, g_height = 480;
static u32    g_frames;
static u32    g_stat_draw_up, g_stat_draw, g_stat_tex, g_stat_clear, g_stat_skipped;
static void   tex_invalidate(const void* res);
static void   default_xbox_state(void);
static void   maybe_screenshot(void);
static XRes   g_backbuffer, g_depth;
static XRes*  g_textures[4];
static float  g_transforms[16][16];
static u32    g_viewport[6];

/* Xbox D3D globals the game reads directly */
#define XBOX_D3D_g_pDevice  (*(u32*)0x001e2d28)
#define XBOX_D3D_DEVICE     0x001e2d30

static u32 WINAPI vblank_thread(void* arg);

static s32 WINAPI wndproc(HWND h, u32 msg, u32 wp, s32 lp)
{
    if (msg == WM_CLOSE || msg == WM_DESTROY) ExitProcess(0);
    return DefWindowProcA(h, msg, wp, lp);
}

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
}

typedef HRESULT (WINAPI *PFN_CreateDevice)(IUnk*, u32, u32, HWND, u32, D3DPRESENT_PARAMETERS9*, IUnk**);
typedef HRESULT (WINAPI *PFN_Clear)(IUnk*, u32, const void*, u32, u32, float, u32);
typedef HRESULT (WINAPI *PFN_Present)(IUnk*, const void*, const void*, HWND, const void*);
typedef HRESULT (WINAPI *PFN_Scene)(IUnk*);

/* Direct3D_CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppDevice) */
HRESULT WINAPI pc_CreateDevice(u32 adapter, u32 type, HWND focus, u32 flags, u32* pp, u32* ppDevice)
{
    if (pp && pp[0] && pp[1]) { g_width = pp[0]; g_height = pp[1]; }
    start_watchdog();
    CreateThread(NULL, 0, vblank_thread, NULL, 0, NULL);
    log_hex("[pc] CreateDevice width", g_width);
    log_hex("[pc] CreateDevice height", g_height);

    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSA wc = { 0 };
    wc.proc = wndproc; wc.inst = inst; wc.name = "OogiesRevenge";
    wc.cursor = LoadCursorA(NULL, (const char*)32512);
    RegisterClassA(&wc);
    RECT r = { 0, 0, (s32)g_width * 2, (s32)g_height * 2 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, 0);
    g_hwnd = CreateWindowExA(0, "OogiesRevenge", "The Nightmare Before Christmas: Oogie's Revenge",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);

    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) { log_str("[pc] Direct3DCreate9 failed\n"); return (HRESULT)0x80004005; }
    D3DPRESENT_PARAMETERS9 p = { 0 };
    p.BackBufferWidth = g_width; p.BackBufferHeight = g_height;
    p.BackBufferFormat = D3DFMT_X8R8G8B8; p.BackBufferCount = 1;
    p.SwapEffect = 3;  /* COPY: the game does not clear every frame; Xbox keeps the buffer */ p.hDeviceWindow = g_hwnd; p.Windowed = 1;
    p.EnableAutoDepthStencil = 1; p.AutoDepthStencilFormat = D3DFMT_D24S8;
    p.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    HRESULT hr = VCALL(g_d3d, D3D9_CreateDevice, PFN_CreateDevice)(g_d3d, 0, D3DDEVTYPE_HAL, g_hwnd,
                     D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED,
                     &p, &g_dev);
    log_hex("[pc] IDirect3D9::CreateDevice", (u32)hr);
    if (hr < 0) return hr;
    VCALL(g_dev, DEV_BeginScene, PFN_Scene)(g_dev);

    /* Xbox-side objects the game asks for */
    g_backbuffer.Common = XCOMMON_TYPE_SURFACE | 1;
    g_backbuffer.Format = 0x1E << 8;                 /* LIN_X8R8G8B8 */
    g_backbuffer.Size = (g_width - 1) | ((g_height - 1) << 12) | (((g_width * 4) / 64 - 1) << 24);
    g_backbuffer.width = g_width; g_backbuffer.height = g_height; g_backbuffer.xfmt = 0x1E;
    g_depth = g_backbuffer;
    g_depth.Format = 0x2E << 8; g_depth.xfmt = 0x2E;  /* LIN_D24S8 */

    /* fields of the Xbox device object that D3DX (original code) reads */
    *(XRes**)(XBOX_D3D_DEVICE + 0x1a18) = &g_backbuffer;   /* render target */
    *(XRes**)(XBOX_D3D_DEVICE + 0x1a1c) = &g_depth;        /* depth stencil */
    XBOX_D3D_g_pDevice = XBOX_D3D_DEVICE;
    if (ppDevice) *ppDevice = XBOX_D3D_DEVICE;
    g_viewport[2] = g_width; g_viewport[3] = g_height; ((float*)g_viewport)[5] = 1.0f;
    default_xbox_state();
    return 0;
}

/* Clear(Count, pRects, Flags, Color, Z, Stencil) — Xbox flags: Z=1, STENCIL=2, TARGET=0xF0 */
HRESULT WINAPI pc_Clear(u32 count, const void* rects, u32 flags, u32 color, float z, u32 stencil)
{
    g_stat_clear++;
    if (!g_dev) return 0;
    u32 f = 0;
    if (flags & 0xF0) f |= D3DCLEAR_TARGET;
    if (flags & 1) f |= D3DCLEAR_ZBUFFER;
    if (flags & 2) f |= D3DCLEAR_STENCIL;
    return VCALL(g_dev, DEV_Clear, PFN_Clear)(g_dev, count, rects, f, color, z, stencil);
}

/* Swap(Flags) */
u32 WINAPI pc_Swap(u32 flags)
{
    if (g_dev) {
        maybe_screenshot();
        VCALL(g_dev, DEV_EndScene, PFN_Scene)(g_dev);
        VCALL(g_dev, DEV_Present, PFN_Present)(g_dev, NULL, NULL, NULL, NULL);
        VCALL(g_dev, DEV_BeginScene, PFN_Scene)(g_dev);
    }
    pump();
    if ((++g_frames & 63) == 1) {
        log_hex("[pc] frame", g_frames);
        log_hex("[pc]   DrawVerticesUP", g_stat_draw_up);
        log_hex("[pc]   DrawVertices", g_stat_draw);
        log_hex("[pc]   textures", g_stat_tex);
        log_hex("[pc]   clears", g_stat_clear);
        log_hex("[pc]   skipped (vertex shader)", g_stat_skipped);
    }
    return 0;
}

/* -------------------------------------------------------------- resources */

/* CreateTexture2(Width, Height, Depth, Levels, Usage, Format, ResourceType) -> D3DTexture* */
XRes* WINAPI pc_CreateTexture2(u32 w, u32 h, u32 depth, u32 levels, u32 usage, u32 fmt, u32 rtype)
{
    XRes* t = new_res(XCOMMON_TYPE_TEXTURE);
    g_stat_tex++;
    if (!levels) { levels = 1; for (u32 m = w > h ? w : h; m > 1; m >>= 1) levels++; }
    t->width = w; t->height = h; t->levels = levels; t->xfmt = fmt;
    u32 total = 0;
    for (u32 l = 0, lw = w, lh = h; l < levels; l++, lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1)
        total += level_bytes(fmt, lw, lh);
    t->bytes = total;
    t->mem = phys_alloc(total, PAGE);
    t->Data = (u32)t->mem & 0x0FFFFFFFu;     /* physical address, like the Xbox */
    t->Format = (2 << 4) | (fmt << 8) | (levels << 16) | (log2u(w) << 20) | (log2u(h) << 24);
    return t;
}

static u8* level_ptr(XRes* t, u32 level, u32* pw, u32* ph)
{
    u32 off = 0, lw = t->width, lh = t->height;
    for (u32 l = 0; l < level; l++) {
        off += level_bytes(t->xfmt, lw, lh);
        lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    *pw = lw; *ph = lh;
    return t->mem + off;
}

/* D3DTexture_LockRect(pTexture, Level, pLockedRect, pRect, Flags) */
HRESULT WINAPI pc_LockRect(XRes* t, u32 level, D3DLOCKED_RECT* lr, const RECT* rect, u32 flags)
{
    u32 w, h;
    u8* p = level_ptr(t, level, &w, &h);
    tex_invalidate(t);
    lr->pBits = p;
    lr->Pitch = xfmt_dxt(t->xfmt) ? ((w + 3) / 4) * (t->xfmt == 0x0C ? 8 : 16) : w * xfmt_bpp(t->xfmt) / 8;
    return 0;
}

/* D3DTexture_GetSurfaceLevel2(pTexture, Level) -> D3DSurface* */
XRes* WINAPI pc_GetSurfaceLevel2(XRes* t, u32 level)
{
    XRes* s = new_res(XCOMMON_TYPE_SURFACE);
    u32 w, h;
    s->mem = level_ptr(t, level, &w, &h);
    s->Data = (u32)s->mem & 0x0FFFFFFFu;
    s->width = w; s->height = h; s->xfmt = t->xfmt; s->levels = 1; s->Parent = t;
    s->Format = (t->Format & ~0x000F0000u) | (1 << 16);
    s->Size = (w - 1) | ((h - 1) << 12);
    return s;
}

/* D3DSurface_GetDesc(pSurface, pDesc): Format, Type, Usage, Size, MultiSampleType, Width, Height */
void WINAPI pc_SurfaceGetDesc(XRes* s, u32* d)
{
    d[0] = s->xfmt; d[1] = 1; d[2] = 0;
    d[3] = level_bytes(s->xfmt, s->width, s->height);
    d[4] = 0; d[5] = s->width; d[6] = s->height;
}

/* CreateVertexBuffer2(Length) -> D3DVertexBuffer* */
XRes* WINAPI pc_CreateVertexBuffer2(u32 len)
{
    XRes* v = new_res(XCOMMON_TYPE_VERTEXBUFFER);
    v->mem = phys_alloc(len, PAGE); v->bytes = len; v->Data = (u32)v->mem & 0x0FFFFFFFu;
    return v;
}

/* D3DVertexBuffer_Lock2(pVB, Flags) -> BYTE* */
u8* WINAPI pc_VertexBufferLock2(XRes* v, u32 flags) { return v->mem; }

u32 WINAPI pc_ResourceAddRef(XRes* r) { return ++r->Common & 0xFFFF; }
u32 WINAPI pc_ResourceRelease(XRes* r) { if (r->Common & 0xFFFF) r->Common--; return r->Common & 0xFFFF; }
/* D3DResource_Register(pResource, pBase): Data is an offset from pBase */
void WINAPI pc_ResourceRegister(XRes* r, u32 base) { r->Data += base; }
void WINAPI pc_ResourceBlockUntilNotBusy(XRes* r) { }

XRes* WINAPI pc_GetBackBuffer2(s32 index) { return &g_backbuffer; }
XRes* WINAPI pc_GetRenderTarget2(void) { return &g_backbuffer; }
XRes* WINAPI pc_GetDepthStencilSurface2(void) { return &g_depth; }
HRESULT WINAPI pc_SetRenderTarget(XRes* rt, XRes* zs) { return 0; }
HRESULT WINAPI pc_CopyRects(XRes* src, const void* srcRects, u32 n, XRes* dst, const void* pts) { return 0; }

/* SetTexture(Stage, pTexture) */
HRESULT WINAPI pc_SetTexture(u32 stage, XRes* t) { if (stage < 4) g_textures[stage] = t; return 0; }
XRes* WINAPI pc_GetTexture2(u32 stage) { XRes* t = stage < 4 ? g_textures[stage] : NULL; if (t) t->Common++; return t; }

/* ---------------------------------------------------------------- state */

HRESULT WINAPI pc_SetTransform(u32 state, const float* m)
{
    if (state < 16) memcpy(g_transforms[state], m, 64);
    return 0;
}
HRESULT WINAPI pc_GetTransform(u32 state, float* m)
{
    if (state < 16) memcpy(m, g_transforms[state], 64);
    return 0;
}
HRESULT WINAPI pc_SetViewport(const u32* vp) { memcpy(g_viewport, vp, 24); return 0; }
HRESULT WINAPI pc_GetViewport(u32* vp) { memcpy(vp, g_viewport, 24); return 0; }
HRESULT WINAPI pc_GetScissors(u32* count, BOOL* exclusive, void* rects)
{
    if (count) *count = 0;
    if (exclusive) *exclusive = 0;
    return 0;
}
HRESULT WINAPI pc_SetScissors(u32 count, BOOL exclusive, const void* rects) { return 0; }
HRESULT WINAPI pc_GetTile(u32 index, u32* tile) { memset(tile, 0, 24); return 0; }
HRESULT WINAPI pc_SetTile(u32 index, const u32* tile) { return 0; }
void WINAPI pc_SetScreenSpaceOffset(float x, float y) { }
void WINAPI pc_SetGammaRamp(u32 flags, const void* ramp) { }

void FASTCALL pc_SetRenderState_Simple(u32 method, u32 value) { }
void WINAPI pc_SetRenderState_1(u32 value) { }      /* FogColor, CullMode, ZEnable, ... */
void WINAPI pc_SetTextureState_TexCoordIndex(u32 stage, u32 value) { }

/* ---------------------------------------------------------------- shaders */

static u32 g_pshader;
/* vertex shaders: vshader.c */
HRESULT WINAPI pc_GetVertexShaderSize(u32 handle, u32* size) { *size = 0; return 0; }
HRESULT WINAPI pc_SetShaderConstantMode(u32 mode) { return 0; }
HRESULT WINAPI pc_CreatePixelShader(const void* def, u32* handle) { static u32 next = 1; *handle = next++; return 0; }
HRESULT WINAPI pc_SetPixelShader(u32 handle) { g_pshader = handle; return 0; }
HRESULT WINAPI pc_GetPixelShader(u32* handle) { *handle = g_pshader; return 0; }
HRESULT WINAPI pc_SetPixelShaderProgram(const void* def) { return 0; }

/* ------------------------------------------------------------------ draw */

static XRes* g_stream[16]; static u32 g_stride[16];
HRESULT WINAPI pc_SetStreamSource(u32 n, XRes* vb, u32 stride) { if (n < 16) { g_stream[n] = vb; g_stride[n] = stride; } return 0; }
/* pc_DrawVerticesUP / pc_DrawVertices: draw.c */

/* ------------------------------------------------------------------ misc */

void WINAPI pc_SetPushBufferSize(u32 size, u32 kickoff) { }
void WINAPI pc_Set1(u32 v) { }          /* single-argument device field setters */

/* Vertical-blank callbacks (device +0x1db4 / +0x1db8). On the Xbox they run
 * from the GPU's vblank interrupt; here a 60 Hz thread calls them. The game
 * paces its frames on them (event set by one, frame counter in the other). */
typedef void (*VBLANKCB)(void*);
static volatile VBLANKCB g_vblank_cb[2];
static u32 g_vblank_data[3];             /* D3DVBLANKDATA: VBlank, Swap, Flags */

void WINAPI pc_SetVBlankCallback_1db4(VBLANKCB cb) { g_vblank_cb[0] = cb; }
void WINAPI pc_SetVBlankCallback_1db8(VBLANKCB cb) { g_vblank_cb[1] = cb; }

IMPORT HANDLE WINAPI CreateWaitableTimerA(void* sa, BOOL manual, const char* name);
IMPORT BOOL   WINAPI SetWaitableTimer(HANDLE t, const u64* due, s32 period, void* fn, void* arg, BOOL resume);
IMPORT u32    WINAPI WaitForSingleObject(HANDLE h, u32 ms);

static u32 WINAPI vblank_thread(void* arg)
{
    HANDLE t = CreateWaitableTimerA(NULL, 0, NULL);
    u64 due = (u64)-166667;                  /* 16.7 ms, relative */
    SetWaitableTimer(t, &due, 17, NULL, NULL, 0);
    for (;;) {
        WaitForSingleObject(t, 0xFFFFFFFF);
        g_vblank_data[0]++;
        g_vblank_data[1] = g_frames;
        for (int i = 0; i < 2; i++)
            if (g_vblank_cb[i]) g_vblank_cb[i](g_vblank_data);
    }
}
u32  WINAPI pc_Get1(u32 v) { return 0; }
void WINAPI pc_BlockUntilVerticalBlank(void) { Sleep(0); }
