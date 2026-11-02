/*
 * Vertex shaders for the PC renderer.
 *
 * The game's 14 NV2A vertex programs are translated to HLSL at build time
 * (tools/nv2a_vsh.py -> build/pc/vshaders.inc) and compiled here with
 * d3dcompiler when the game creates them. Constants live where the Xbox D3D
 * keeps its shadow copy (0x1e1c70, 192 registers, hardware numbering), so
 * code reading that array still sees the right values.
 */
#include "win32.h"
#include "vshaders.inc"

#define XVSC ((float*)0x001e1c70)                 /* Xbox D3D vertex shader constant shadow */

IMPORT HRESULT WINAPI D3DCompile(const void* src, size_t len, const char* name, const void* defines, void* include,
                                 const char* entry, const char* target, u32 f1, u32 f2, IUnk** code, IUnk** errors);

typedef void*   (WINAPI *PFN_BlobPtr)(IUnk*);
typedef size_t  (WINAPI *PFN_BlobSize)(IUnk*);
typedef HRESULT (WINAPI *PFN_CreateVS)(IUnk*, const void*, IUnk**);
typedef HRESULT (WINAPI *PFN_CreateDecl)(IUnk*, const void*, IUnk**);
typedef HRESULT (WINAPI *PFN_SetObj)(IUnk*, IUnk*);
typedef HRESULT (WINAPI *PFN_SetVSConstF)(IUnk*, u32, const float*, u32);
typedef HRESULT (WINAPI *PFN_SetFVF)(IUnk*, u32);

#define DEV_CreateVertexDeclaration 86
#define DEV_SetVertexDeclaration    87
#define DEV_SetVertexShader9        92
#define DEV_SetVertexShaderConstantF 94

static IUnk* g_vs9[VSH_COUNT];
static IUnk* g_decl9[VSH_COUNT];
static u32   g_vshader;                           /* Xbox handle: odd = shader, even = FVF */

static int vsh_index(u32 handle) { return (handle & 1) ? (int)(handle >> 1) - 1 : -1; }

/* CreateVertexShader(pDeclaration, pFunction, pHandle, Usage) */
HRESULT WINAPI pc_CreateVertexShader(const u32* decl, const u32* fn, u32* handle, u32 usage)
{
    int i = 0;
    while (i < VSH_COUNT && g_vsh_fn[i] != (u32)fn) i++;
    if (i == VSH_COUNT) { log_hex("[pc] unknown vertex shader", (u32)fn); return (HRESULT)0x80004005; }
    *handle = ((u32)(i + 1) << 1) | 1;
    if (g_vs9[i] || !g_dev) return 0;

    const char* src = g_vsh_src[i];
    size_t n = 0; while (src[n]) n++;
    IUnk *code = NULL, *err = NULL;
    /* full optimisation keeps the temporaries inside vs_2_0's twelve; vs_2_a has more */
    HRESULT hr = D3DCompile(src, n, "xbox_vsh", NULL, NULL, "main", "vs_2_0", 1u << 15, 0, &code, &err);
    if (hr < 0) {
        if (err) VCALL(err, 2, PFN_Scene)(err);
        err = NULL;
        hr = D3DCompile(src, n, "xbox_vsh", NULL, NULL, "main", "vs_2_a", 1u << 15, 0, &code, &err);
    }
    if (hr < 0) {
        log_hex("[pc] vertex shader compile failed", i);
        if (err) { log_str((const char*)VCALL(err, 3, PFN_BlobPtr)(err)); log_str("\n"); }
        return 0;
    }
    VCALL(g_dev, 91, PFN_CreateVS)(g_dev, VCALL(code, 3, PFN_BlobPtr)(code), &g_vs9[i]);
    VCALL(g_dev, DEV_CreateVertexDeclaration, PFN_CreateDecl)(g_dev, g_vsh_decl[i], &g_decl9[i]);
    VCALL(code, 2, PFN_Scene)(code);
    log_hex("[pc] vertex shader ready", i);
    return 0;
}

/* constants a shader's declaration carries are loaded when it is selected */
static void select_vshader(u32 handle)
{
    g_vshader = handle;
    int i = vsh_index(handle);
    if (i < 0) return;
    for (u32 k = 0; g_vsh_consts[k][0] != 0xFFFFFFFF; k++)
        if (g_vsh_consts[k][0] == (u32)i) memcpy(XVSC + g_vsh_consts[k][1] * 4, &g_vsh_consts[k][2], 16);
}

HRESULT WINAPI pc_SetVertexShader(u32 handle) { select_vshader(handle); return 0; }
HRESULT WINAPI pc_LoadVertexShader(u32 handle, u32 addr) { select_vshader(handle); return 0; }
HRESULT WINAPI pc_SelectVertexShader(u32 handle, u32 addr) { select_vshader(handle); return 0; }
HRESULT WINAPI pc_GetVertexShader(u32* handle) { *handle = g_vshader; return 0; }

/* SetVertexShaderConstant1/4(Register (hardware), pConstantData); NotInline adds a DWORD count */
void FASTCALL pc_SetVertexShaderConstant1(u32 reg, const void* data) { if (reg < 192) memcpy(XVSC + reg * 4, data, 16); }
void FASTCALL pc_SetVertexShaderConstant4(u32 reg, const void* data) { if (reg < 189) memcpy(XVSC + reg * 4, data, 64); }
void FASTCALL pc_SetVertexShaderConstantNotInline(u32 reg, const void* data, u32 dwords)
{
    if (reg * 4 + dwords <= 192 * 4) memcpy(XVSC + reg * 4, data, dwords * 4);
}

/* What the device has, so a draw only sends what changed. */
static IUnk* g_bound_vs9 = (IUnk*)1, *g_bound_decl9 = (IUnk*)1;
static u32   g_bound_fvf = 0xFFFFFFFF;
static float g_consts9[192 * 4];
static int   g_consts9_valid;

/* constants changed since the last draw, sent as one range */
static void upload_constants(void)
{
    const u32* now = (const u32*)XVSC, *dev = (const u32*)g_consts9;
    u32 lo = 192, hi = 0;
    for (u32 r = 0; r < 192; r++) {
        const u32* a = now + r * 4, *b = dev + r * 4;
        if (!g_consts9_valid || a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3]) {
            if (r < lo) lo = r;
            hi = r + 1;
        }
    }
    g_consts9_valid = 1;
    if (lo >= hi) return;
    memcpy(g_consts9 + lo * 4, XVSC + lo * 4, (hi - lo) * 16);
    VCALL(g_dev, DEV_SetVertexShaderConstantF, PFN_SetVSConstF)(g_dev, lo, XVSC + lo * 4, hi - lo);
}

/* Bind the current shader for a draw. Returns 0 if it cannot be drawn. */
static int bind_vshader(void)
{
    int i = vsh_index(g_vshader);
    if (i < 0) {                                         /* fixed-function FVF */
        if (g_bound_vs9) { VCALL(g_dev, DEV_SetVertexShader9, PFN_SetObj)(g_dev, NULL); g_bound_vs9 = NULL; }
        if (g_bound_fvf != g_vshader) {
            VCALL(g_dev, DEV_SetFVF, PFN_SetFVF)(g_dev, g_vshader);
            g_bound_fvf = g_vshader; g_bound_decl9 = (IUnk*)1;   /* SetFVF replaces the declaration */
        }
        return 1;
    }
    if (!g_vs9[i]) return 0;
    /* viewport scale / offset the Xbox D3D keeps in c[-38] / c[-37] (hw 58 / 59);
     * the translated shaders undo them to get back to clip space */
    const float* vp = (const float*)g_viewport;
    float w = (float)g_viewport[2], h = (float)g_viewport[3], zs = 16777215.0f;
    float* c58 = XVSC + 58 * 4, *c59 = XVSC + 59 * 4;
    c58[0] = w * 0.5f; c58[1] = -h * 0.5f; c58[2] = zs * (vp[5] - vp[4]); c58[3] = 1.0f;
    c59[0] = (float)g_viewport[0] + w * 0.5f; c59[1] = (float)g_viewport[1] + h * 0.5f; c59[2] = zs * vp[4]; c59[3] = 0.0f;
    if (c58[2] == 0.0f) c58[2] = zs;
    if (g_bound_decl9 != g_decl9[i]) {
        VCALL(g_dev, DEV_SetVertexDeclaration, PFN_SetObj)(g_dev, g_decl9[i]);
        g_bound_decl9 = g_decl9[i]; g_bound_fvf = 0xFFFFFFFF;
    }
    if (g_bound_vs9 != g_vs9[i]) { VCALL(g_dev, DEV_SetVertexShader9, PFN_SetObj)(g_dev, g_vs9[i]); g_bound_vs9 = g_vs9[i]; }
    upload_constants();
    return 1;
}
