/*
 * Pixel shaders for the PC renderer.
 *
 * The game's NV2A register-combiner setups (D3DPIXELSHADERDEF) are translated
 * to HLSL at build time (tools/nv2a_psh.py -> build/pc/pshaders.inc) and
 * compiled here with d3dcompiler. They are found by the address of their
 * definition, both for CreatePixelShader and for SetPixelShaderProgram.
 */
#include "win32.h"
#include "pshaders.inc"

#define DEV_CreatePixelShader9 106
#define DEV_SetPixelShader9    107
#define DEV_SetPixelShaderConstantF 109

static IUnk* g_ps9[PSH_COUNT];
static u32   g_pshader;                           /* Xbox handle: index + 1, 0 = none */
static IUnk* g_bound_ps9 = (IUnk*)1;

static int psh_index(const void* def)
{
    for (int i = 0; i < PSH_COUNT; i++) if (g_psh_def[i] == (u32)def) return i;
    return -1;
}

static void compile_pshader(int i)
{
    if (g_ps9[i] || !g_dev) return;
    const char* src = g_psh_src[i];
    size_t n = 0; while (src[n]) n++;
    static const char* const targets[] = { "ps_2_0", "ps_2_b", "ps_2_a" };
    IUnk *code = NULL, *err = NULL;
    HRESULT hr = -1;
    for (int t = 0; t < 3 && hr < 0; t++) {
        if (err) { VCALL(err, 2, PFN_Scene)(err); err = NULL; }
        hr = D3DCompile(src, n, "xbox_psh", NULL, NULL, "main", targets[t], 1u << 15, 0, &code, &err);
    }
    if (hr < 0) {
        log_hex("[pc] pixel shader compile failed", i);
        if (err) { log_str((const char*)VCALL(err, 3, PFN_BlobPtr)(err)); log_str("\n"); }
        return;
    }
    VCALL(g_dev, DEV_CreatePixelShader9, PFN_CreateVS)(g_dev, VCALL(code, 3, PFN_BlobPtr)(code), &g_ps9[i]);
    VCALL(code, 2, PFN_Scene)(code);
    log_hex("[pc] pixel shader ready", i);
}

/* CreatePixelShader(pPSDef, pHandle) */
HRESULT WINAPI pc_CreatePixelShader(const void* def, u32* handle)
{
    int i = psh_index(def);
    if (i < 0) log_hex("[pc] unknown pixel shader", (u32)def);
    else compile_pshader(i);
    *handle = (u32)(i + 1);
    return 0;
}
HRESULT WINAPI pc_SetPixelShader(u32 handle) { g_pshader = handle; return 0; }
HRESULT WINAPI pc_GetPixelShader(u32* handle) { *handle = g_pshader; return 0; }
HRESULT WINAPI pc_SetPixelShaderProgram(const void* def)
{
    int i = def ? psh_index(def) : -1;
    if (i >= 0) compile_pshader(i);
    g_pshader = (u32)(i + 1);
    return 0;
}

/* Bind the current pixel shader for a draw; 1 if one is active (the texture
 * stages then only supply textures and sampler state). */
static int bind_pshader(void)
{
    IUnk* ps = g_pshader && g_pshader <= PSH_COUNT ? g_ps9[g_pshader - 1] : NULL;
    if (g_bound_ps9 != ps) { VCALL(g_dev, DEV_SetPixelShader9, PFN_SetObj)(g_dev, ps); g_bound_ps9 = ps; }
    return ps != NULL;
}
