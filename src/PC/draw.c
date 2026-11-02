/*
 * Drawing for the PC renderer: Xbox D3D state, textures and draw calls on
 * Direct3D 9.
 *
 * The game keeps render / texture-stage state in the Xbox D3D globals
 * D3D__RenderState and D3D__TextureState (it writes most of them inline), so
 * every draw reads those arrays and applies them. Xbox textures are found by
 * their resource header (Format / Size / Data) and uploaded once: swizzled
 * formats are unswizzled and converted to A8R8G8B8, DXT goes as is.
 */
#include "win32.h"

IMPORT u32    WINAPI GetEnvironmentVariableA(const char* name, char* buf, u32 n);
IMPORT HANDLE WINAPI CreateFileA(const char* name, u32 access, u32 share, void* sa, u32 disp, u32 flags, HANDLE tmpl);

#define XRS(i)      (((volatile u32*)0x001e2a94)[i])            /* D3D__RenderState */
#define XTS(s, i)   (((volatile u32*)0x001e2890)[(s) * 32 + (i)]) /* D3D__TextureState */

/* Xbox D3DRENDERSTATETYPE (XDK 5849). Checked against the game's own code: each
 * slot is written right before the matching NV2A method (e.g. 0x207 to slot 69
 * before NV097_SET_STENCIL_FUNC) and against the setters of the complex states. */
enum { XRS_ZFUNC = 56, XRS_ALPHAFUNC, XRS_ALPHABLENDENABLE, XRS_ALPHATESTENABLE, XRS_ALPHAREF,
       XRS_SRCBLEND, XRS_DESTBLEND, XRS_ZWRITEENABLE, XRS_DITHERENABLE, XRS_SHADEMODE,
       XRS_COLORWRITEENABLE, XRS_BLENDOP = 73, XRS_FOGENABLE = 91, XRS_LIGHTING = 101,
       XRS_FOGCOLOR = 137, XRS_ZENABLE = 142, XRS_STENCILENABLE, XRS_CULLMODE = 146, XRS_TEXTUREFACTOR };
/* Xbox D3DTEXTURESTAGESTATETYPE */
enum { XTS_ADDRESSU, XTS_ADDRESSV, XTS_ADDRESSW, XTS_MAGFILTER, XTS_MINFILTER, XTS_MIPFILTER,
       XTS_COLOROP = 12, XTS_COLORARG0, XTS_COLORARG1, XTS_COLORARG2, XTS_ALPHAOP, XTS_ALPHAARG0,
       XTS_ALPHAARG1, XTS_ALPHAARG2, XTS_RESULTARG, XTS_TEXTURETRANSFORMFLAGS, XTS_TEXCOORDINDEX = 28 };

/* Direct3D 9 enums used here */
enum { RS_ZENABLE = 7, RS_SHADEMODE = 9, RS_ZWRITEENABLE = 14, RS_ALPHATESTENABLE = 15, RS_SRCBLEND = 19,
       RS_DESTBLEND = 20, RS_CULLMODE = 22, RS_ZFUNC = 23, RS_ALPHAREF = 24, RS_ALPHAFUNC = 25,
       RS_ALPHABLENDENABLE = 27, RS_FOGENABLE = 28, RS_STENCILENABLE = 52, RS_TEXTUREFACTOR = 60,
       RS_LIGHTING = 137, RS_COLORWRITEENABLE = 168, RS_BLENDOP = 171 };
enum { TSS_COLOROP = 1, TSS_COLORARG1, TSS_COLORARG2, TSS_ALPHAOP, TSS_ALPHAARG1, TSS_ALPHAARG2,
       TSS_TEXCOORDINDEX = 11, TSS_TEXTURETRANSFORMFLAGS = 24, TSS_COLORARG0 = 26, TSS_ALPHAARG0, TSS_RESULTARG };
enum { SAMP_ADDRESSU = 1, SAMP_ADDRESSV, SAMP_ADDRESSW, SAMP_BORDERCOLOR, SAMP_MAGFILTER, SAMP_MINFILTER, SAMP_MIPFILTER };

typedef HRESULT (WINAPI *PFN_SetRS)(IUnk*, u32, u32);
typedef HRESULT (WINAPI *PFN_SetTSS)(IUnk*, u32, u32, u32);
typedef HRESULT (WINAPI *PFN_SetTex)(IUnk*, u32, IUnk*);
typedef HRESULT (WINAPI *PFN_SetXf)(IUnk*, u32, const float*);
typedef HRESULT (WINAPI *PFN_CreateTex)(IUnk*, u32, u32, u32, u32, u32, u32, IUnk**, void*);
typedef HRESULT (WINAPI *PFN_TexLock)(IUnk*, u32, D3DLOCKED_RECT*, const void*, u32);
typedef HRESULT (WINAPI *PFN_TexUnlock)(IUnk*, u32);

#define TEX_LockRect   19
#define TEX_UnlockRect 20

/* Every draw applies the whole Xbox state, so the Direct3D 9 calls go through
 * a cache and only changes reach the device (each call is costly, above all
 * under Wine). */
static u32 g_rs9[256], g_tss9[4][32], g_samp9[4][16];
static u8  g_rs9_set[256], g_tss9_set[4][32], g_samp9_set[4][16];
static IUnk* g_tex9[4];
static float g_xf9[4][16];                   /* view, projection, world, (unused) */

static void rs(u32 s, u32 v)
{
    if (s < 256) { if (g_rs9_set[s] && g_rs9[s] == v) return; g_rs9_set[s] = 1; g_rs9[s] = v; }
    VCALL(g_dev, DEV_SetRenderState, PFN_SetRS)(g_dev, s, v);
}
static void tss(u32 st, u32 s, u32 v)
{
    if (st < 4 && s < 32) { if (g_tss9_set[st][s] && g_tss9[st][s] == v) return; g_tss9_set[st][s] = 1; g_tss9[st][s] = v; }
    VCALL(g_dev, DEV_SetTextureStageState, PFN_SetTSS)(g_dev, st, s, v);
}
static void samp(u32 st, u32 s, u32 v)
{
    if (st < 4 && s < 16) { if (g_samp9_set[st][s] && g_samp9[st][s] == v) return; g_samp9_set[st][s] = 1; g_samp9[st][s] = v; }
    VCALL(g_dev, DEV_SetSamplerState, PFN_SetTSS)(g_dev, st, s, v);
}
static void set_tex9(u32 st, IUnk* t)
{
    if (g_tex9[st] == t) return;
    g_tex9[st] = t;
    VCALL(g_dev, DEV_SetTexture, PFN_SetTex)(g_dev, st, t);
}
static int same16(const float* a, const float* b) { const u32* x = (const u32*)a, *y = (const u32*)b; for (int i = 0; i < 16; i++) if (x[i] != y[i]) return 0; return 1; }

/* ---------------------------------------------------------- enum mapping */

static u32 map_blend(u32 v)       /* Xbox uses GL blend factors */
{
    if (v == 0) return 1;                                  /* ZERO */
    if (v == 1) return 2;                                  /* ONE */
    if (v >= 0x300 && v <= 0x308) return v - 0x300 + 3;    /* SRCCOLOR .. SRCALPHASAT */
    return 2;
}
static u32 map_cmp(u32 v) { return (v >= 0x200 && v <= 0x207) ? v - 0x200 + 1 : 8; }
static u32 map_cull(u32 v) { return v == 0x900 ? 2 : v == 0x901 ? 3 : 1; }
static u32 map_blendop(u32 v)
{
    switch (v) { case 0x800a: return 2; case 0x800b: return 3; case 0x8007: return 4; case 0x8008: return 5; }
    return 1;
}
static u32 map_top(u32 v)          /* Xbox D3DTEXTUREOP -> D3D9 */
{
    static const u8 t[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 };
    return v < sizeof(t) ? t[v] : 1;
}
static u32 map_colorwrite(u32 v)   /* Xbox: R<<16, G<<8, B, A<<24 */
{
    return ((v >> 16) & 1) | (((v >> 8) & 1) << 1) | ((v & 1) << 2) | (((v >> 24) & 1) << 3);
}

/* ---------------------------------------------------------- texture cache */

typedef struct { u32 key, data, format, size; IUnk* tex; } TexEntry;
static TexEntry g_tex_cache[1024];

static void tex_invalidate(const void* res)
{
    for (int i = 0; i < 1024; i++)
        if (g_tex_cache[i].key == (u32)res) g_tex_cache[i].key = 0;
}

/* Morton-order unswizzle helpers */
static void swizzle_masks(u32 w, u32 h, u32* mx, u32* my)
{
    u32 bit = 1; *mx = *my = 0;
    for (u32 i = 1; i < w || i < h; i <<= 1) {
        if (i < w) { *mx |= bit; bit <<= 1; }
        if (i < h) { *my |= bit; bit <<= 1; }
    }
}

static u32 texel_argb(u32 fmt, const u8* p)
{
    u32 v;
    switch (fmt) {
    case 0x06: case 0x12: return *(const u32*)p;                         /* A8R8G8B8 */
    case 0x07: case 0x1E: return *(const u32*)p | 0xFF000000;            /* X8R8G8B8 */
    case 0x3A: case 0x3F: v = *(const u32*)p;                            /* A8B8G8R8 */
        return (v & 0xFF00FF00) | ((v >> 16) & 0xFF) | ((v & 0xFF) << 16);
    case 0x04: case 0x1D: v = *(const u16*)p;                            /* A4R4G4B4 */
        return ((v >> 12) * 0x11u) << 24 | (((v >> 8) & 15) * 0x11u) << 16 | (((v >> 4) & 15) * 0x11u) << 8 | (v & 15) * 0x11u;
    case 0x02: case 0x10: case 0x03: case 0x1C: v = *(const u16*)p;      /* A1R5G5B5 / X1R5G5B5 */
        return ((fmt == 0x02 || fmt == 0x10) ? ((v >> 15) ? 0xFF000000u : 0) : 0xFF000000u)
             | (((v >> 10) & 31) * 255 / 31) << 16 | (((v >> 5) & 31) * 255 / 31) << 8 | (v & 31) * 255 / 31;
    case 0x05: case 0x11: v = *(const u16*)p;                            /* R5G6B5 */
        return 0xFF000000u | ((v >> 11) * 255 / 31) << 16 | (((v >> 5) & 63) * 255 / 63) << 8 | (v & 31) * 255 / 31;
    case 0x00: case 0x13: v = *p; return 0xFF000000u | v * 0x010101u;   /* L8 */
    case 0x19: case 0x1F: return (u32)*p << 24 | 0xFFFFFF;               /* A8 */
    case 0x1A: case 0x20: v = *(const u16*)p;                            /* A8L8 */
        return (v >> 8) << 24 | (v & 0xFF) * 0x010101u;
    case 0x01: case 0x1B: v = *p; return v << 24 | v * 0x010101u;        /* AL8 */
    default: return 0xFFFF00FF;                                           /* unknown: magenta */
    }
}

static int fmt_linear(u32 f)
{
    return (f >= 0x10 && f <= 0x20) || f == 0x2E || f == 0x30 || f == 0x35 || f == 0x3F;
}

static void dump_texture(const u32* res, const u8* src, u32 bytes)
{
    char b[4];
    if (!GetEnvironmentVariableA("PC_DUMPTEX", b, 4)) return;
    char name[32] = "tex_00000000.bin";
    for (int i = 0; i < 8; i++) name[4 + i] = "0123456789abcdef"[((u32)res >> (28 - i * 4)) & 15];
    HANDLE f = CreateFileA(name, 0x40000000, 0, NULL, 2, 0x80, NULL);
    u32 w;
    WriteFile(f, res, 20, &w, NULL);                 /* Xbox resource header */
    WriteFile(f, src, bytes, &w, NULL);
    CloseHandle(f);
}

static IUnk* upload_texture(const u32* res)
{
    u32 format = res[3], size = res[4], data = res[1];
    u32 f = (format >> 8) & 0xFF, levels = (format >> 16) & 15;
    u32 w, h, pitch = 0;
    if (size) {                                    /* linear: explicit size and pitch */
        w = (size & 0xFFF) + 1; h = ((size >> 12) & 0xFFF) + 1; pitch = ((size >> 24) + 1) * 64;
        levels = 1;
    } else {
        w = 1u << ((format >> 20) & 15); h = 1u << ((format >> 24) & 15);
    }
    if (!levels) levels = 1;
    const u8* src = (const u8*)(data | 0x80000000u);
    int dxt = xfmt_dxt(f);
    u32 d3dfmt = f == 0x0C ? 0x31545844 : f == 0x0E ? 0x33545844 : f == 0x0F ? 0x35545844 : 21;
    IUnk* tex = NULL;
    if (VCALL(g_dev, DEV_CreateTexture, PFN_CreateTex)(g_dev, w, h, levels, 0, d3dfmt, 1 /* MANAGED */, &tex, NULL) < 0)
        return NULL;
    u32 bpp = xfmt_bpp(f) / 8;
    dump_texture(res, src, level_bytes(f, w, h));
    for (u32 l = 0, lw = w, lh = h; l < levels; l++) {
        D3DLOCKED_RECT lr;
        if (VCALL(tex, TEX_LockRect, PFN_TexLock)(tex, l, &lr, NULL, 0) < 0) break;
        if (dxt) {
            u32 bw = (lw + 3) / 4, bh = (lh + 3) / 4, bs = f == 0x0C ? 8 : 16;
            for (u32 y = 0; y < bh; y++) memcpy((u8*)lr.pBits + y * lr.Pitch, src + y * bw * bs, bw * bs);
            src += bw * bh * bs;
        } else if (fmt_linear(f)) {
            u32 sp = pitch ? pitch : lw * bpp;
            for (u32 y = 0; y < lh; y++) {
                u32* d = (u32*)((u8*)lr.pBits + y * lr.Pitch);
                for (u32 x = 0; x < lw; x++) d[x] = texel_argb(f, src + y * sp + x * bpp);
            }
            src += sp * lh;
        } else {
            u32 mx, my; swizzle_masks(lw, lh, &mx, &my);
            u32 sy = 0;
            for (u32 y = 0; y < lh; y++) {
                u32* d = (u32*)((u8*)lr.pBits + y * lr.Pitch);
                u32 sx = 0;
                for (u32 x = 0; x < lw; x++) {
                    d[x] = texel_argb(f, src + (sx | sy) * bpp);
                    sx = (sx - mx) & mx;
                }
                sy = (sy - my) & my;
            }
            src += lw * lh * bpp;
        }
        VCALL(tex, TEX_UnlockRect, PFN_TexUnlock)(tex, l);
        lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    return tex;
}

static IUnk* get_texture(const u32* res)
{
    if (!res) return NULL;
    u32 slot = ((u32)res >> 4) & 1023;
    for (u32 i = 0; i < 1024; i++) {
        TexEntry* e = &g_tex_cache[(slot + i) & 1023];
        if (e->key == (u32)res && e->data == res[1] && e->format == res[3] && e->size == res[4]) return e->tex;
        if (!e->key) {
            if (e->tex) VCALL(e->tex, 2, PFN_Scene)(e->tex);      /* Release a stale one */
            u32 t = ticks();
            e->tex = upload_texture(res);
            g_us_upload += us_since(t); g_n_upload++;
            e->key = (u32)res; e->data = res[1]; e->format = res[3]; e->size = res[4];
            return e->tex;
        }
    }
    return NULL;
}

/* Defaults the Xbox D3D sets up in CreateDevice (which is replaced). */
static void default_xbox_state(void)
{
    XRS(XRS_ZFUNC) = 0x203;            /* LESSEQUAL */
    XRS(XRS_ALPHAFUNC) = 0x207;        /* ALWAYS */
    XRS(XRS_SRCBLEND) = 1;             /* ONE */
    XRS(XRS_DESTBLEND) = 0;            /* ZERO */
    XRS(XRS_ZWRITEENABLE) = 1;
    XRS(XRS_COLORWRITEENABLE) = 0x01010101;
    XRS(XRS_BLENDOP) = 0x8006;         /* ADD */
    XRS(XRS_ZENABLE) = 1;
    XRS(XRS_CULLMODE) = 0x901;         /* CCW */
    XRS(XRS_LIGHTING) = 1;
    XRS(XRS_TEXTUREFACTOR) = 0xFFFFFFFF;
    for (u32 s = 0; s < 4; s++) {
        XTS(s, XTS_COLOROP) = s ? 1 : 4;            /* MODULATE on stage 0, DISABLE after */
        XTS(s, XTS_COLORARG1) = 2; XTS(s, XTS_COLORARG2) = 1;
        XTS(s, XTS_ALPHAOP) = s ? 1 : 2;            /* SELECTARG1 */
        XTS(s, XTS_ALPHAARG1) = 2; XTS(s, XTS_ALPHAARG2) = 1;
        XTS(s, XTS_ADDRESSU) = XTS(s, XTS_ADDRESSV) = XTS(s, XTS_ADDRESSW) = 1;
        XTS(s, XTS_MAGFILTER) = XTS(s, XTS_MINFILTER) = 2;
        XTS(s, XTS_TEXCOORDINDEX) = s;
    }
}

/* ------------------------------------------------------------ apply state */

static int g_debug_flat = -1;    /* env PC_FLAT=1: vertex colour only, no blending / depth */

static void apply_state(void)
{
    if (g_debug_flat < 0) { char b[4]; g_debug_flat = GetEnvironmentVariableA("PC_FLAT", b, 4) > 0 ? b[0] - '0' : 0; }
    if (g_debug_flat && g_bound_ps9) { VCALL(g_dev, DEV_SetPixelShader9, PFN_SetObj)(g_dev, NULL); g_bound_ps9 = NULL; }
    if (g_debug_flat == 2) {      /* texture x vertex colour x 2, alpha blended */
        rs(RS_ZENABLE, 0); rs(RS_ALPHABLENDENABLE, 1); rs(RS_SRCBLEND, 5); rs(RS_DESTBLEND, 6);
        rs(RS_ALPHATESTENABLE, 0); rs(RS_CULLMODE, 1); rs(RS_LIGHTING, 0); rs(RS_COLORWRITEENABLE, 15);
        IUnk* t = g_textures[0] ? get_texture((const u32*)g_textures[0]) : NULL;
        set_tex9(0, t);
        tss(0, TSS_COLOROP, t ? 5 : 2); tss(0, TSS_COLORARG1, t ? 2 : 0); tss(0, TSS_COLORARG2, 0);
        tss(0, TSS_ALPHAOP, t ? 5 : 2); tss(0, TSS_ALPHAARG1, t ? 2 : 0); tss(0, TSS_ALPHAARG2, 0);
        tss(1, TSS_COLOROP, 1);
        samp(0, SAMP_MAGFILTER, 2); samp(0, SAMP_MINFILTER, 2);
        return;
    }
    if (g_debug_flat) {
        rs(RS_ZENABLE, 0); rs(RS_ALPHABLENDENABLE, 0); rs(RS_ALPHATESTENABLE, 0); rs(RS_CULLMODE, 1);
        rs(RS_LIGHTING, 0); rs(RS_COLORWRITEENABLE, 15);
        tss(0, TSS_COLOROP, 2); tss(0, TSS_COLORARG1, 0); tss(0, TSS_ALPHAOP, 2); tss(0, TSS_ALPHAARG1, 0);
        tss(1, TSS_COLOROP, 1);
        return;
    }
    rs(RS_ZENABLE, XRS(XRS_ZENABLE) ? 1 : 0);
    rs(RS_ZFUNC, map_cmp(XRS(XRS_ZFUNC)));
    rs(RS_ZWRITEENABLE, XRS(XRS_ZWRITEENABLE) ? 1 : 0);
    rs(RS_ALPHABLENDENABLE, XRS(XRS_ALPHABLENDENABLE) ? 1 : 0);
    rs(RS_SRCBLEND, map_blend(XRS(XRS_SRCBLEND)));
    rs(RS_DESTBLEND, map_blend(XRS(XRS_DESTBLEND)));
    rs(RS_BLENDOP, map_blendop(XRS(XRS_BLENDOP)));
    rs(RS_ALPHATESTENABLE, XRS(XRS_ALPHATESTENABLE) ? 1 : 0);
    rs(RS_ALPHAFUNC, map_cmp(XRS(XRS_ALPHAFUNC)));
    rs(RS_ALPHAREF, XRS(XRS_ALPHAREF) & 0xFF);
    rs(RS_CULLMODE, map_cull(XRS(XRS_CULLMODE)));
    rs(RS_LIGHTING, XRS(XRS_LIGHTING) ? 1 : 0);
    rs(RS_FOGENABLE, 0);
    rs(RS_STENCILENABLE, 0);
    rs(RS_TEXTUREFACTOR, XRS(XRS_TEXTUREFACTOR));
    rs(RS_COLORWRITEENABLE, XRS(XRS_COLORWRITEENABLE) ? map_colorwrite(XRS(XRS_COLORWRITEENABLE)) : 15);

    int ps = bind_pshader();
    for (u32 s = 0; s < 4; s++) {
        IUnk* t = g_textures[s] ? get_texture((const u32*)g_textures[s]) : NULL;
        set_tex9(s, t);
        u32 cop = map_top(XTS(s, XTS_COLOROP)), aop = map_top(XTS(s, XTS_ALPHAOP));
        tss(s, TSS_COLOROP, cop);
        tss(s, TSS_COLORARG1, XTS(s, XTS_COLORARG1));
        tss(s, TSS_COLORARG2, XTS(s, XTS_COLORARG2));
        tss(s, TSS_COLORARG0, XTS(s, XTS_COLORARG0));
        tss(s, TSS_ALPHAOP, aop);
        tss(s, TSS_ALPHAARG1, XTS(s, XTS_ALPHAARG1));
        tss(s, TSS_ALPHAARG2, XTS(s, XTS_ALPHAARG2));
        tss(s, TSS_ALPHAARG0, XTS(s, XTS_ALPHAARG0));
        tss(s, TSS_RESULTARG, XTS(s, XTS_RESULTARG) == 5 ? 5 : 1);
        tss(s, TSS_TEXCOORDINDEX, XTS(s, XTS_TEXCOORDINDEX));
        tss(s, TSS_TEXTURETRANSFORMFLAGS, XTS(s, XTS_TEXTURETRANSFORMFLAGS));
        u32 au = XTS(s, XTS_ADDRESSU), av = XTS(s, XTS_ADDRESSV);
        samp(s, SAMP_ADDRESSU, au == 5 ? 3 : au ? au : 1);
        samp(s, SAMP_ADDRESSV, av == 5 ? 3 : av ? av : 1);
        samp(s, SAMP_MAGFILTER, XTS(s, XTS_MAGFILTER) > 3 ? 2 : XTS(s, XTS_MAGFILTER));
        samp(s, SAMP_MINFILTER, XTS(s, XTS_MINFILTER) > 3 ? 2 : XTS(s, XTS_MINFILTER));
        samp(s, SAMP_MIPFILTER, XTS(s, XTS_MIPFILTER) > 3 ? 2 : XTS(s, XTS_MIPFILTER));
        if (cop == 1 && !ps) break;          /* later stages are off too (a pixel shader reads all 4) */
    }

    /* Xbox D3DTS: VIEW 0, PROJECTION 1, TEXTURE0..3 2..5, WORLD 6 */
    /* only the fixed-function pipeline (FVF draws) uses the transforms */
    if (vsh_index(g_vshader) < 0) {
        PFN_SetXf setxf = VCALL(g_dev, DEV_SetTransform, PFN_SetXf);
        static const u32 d3d9_of[3] = { 2, 3, 256 }, xbox_of[3] = { 0, 1, 6 };
        for (u32 k = 0; k < 3; k++)
            if (!same16(g_xf9[k], g_transforms[xbox_of[k]])) {
                memcpy(g_xf9[k], g_transforms[xbox_of[k]], 64);
                setxf(g_dev, d3d9_of[k], g_transforms[xbox_of[k]]);
            }
        for (u32 s = 0; s < 4; s++) setxf(g_dev, 16 + s, g_transforms[2 + s]);
    }
}

/* ------------------------------------------------------------------- draw */

typedef HRESULT (WINAPI *PFN_CreateVB)(IUnk*, u32, u32, u32, u32, IUnk**, void*);
typedef HRESULT (WINAPI *PFN_VBLock)(IUnk*, u32, u32, void**, u32);
typedef HRESULT (WINAPI *PFN_VBUnlock)(IUnk*);
typedef HRESULT (WINAPI *PFN_SetStream)(IUnk*, u32, IUnk*, u32, u32);
typedef HRESULT (WINAPI *PFN_DrawPrim)(IUnk*, u32, u32, u32);
#define DEV_CreateVertexBuffer 26
#define DEV_DrawPrimitive      81
#define DEV_SetStreamSource   100

/* Vertices go through one dynamic ring buffer in SYSTEMMEM: appended with
 * NOOVERWRITE, DISCARD only when it wraps. Under Wine on macOS (wined3d on
 * OpenGL without async buffer mapping) the *UP draws and DEFAULT-pool locks
 * stall or re-upload the whole buffer: 1-10 ms per draw; a SYSTEMMEM buffer
 * only sends the range a draw uses. Quads (Xbox prim 8) use a static index
 * buffer. */
typedef HRESULT (WINAPI *PFN_CreateIB)(IUnk*, u32, u32, u32, u32, IUnk**, void*);
typedef HRESULT (WINAPI *PFN_DrawIdx)(IUnk*, u32, s32, u32, u32, u32, u32);
#define DEV_CreateIndexBuffer   27
#define DEV_DrawIndexedPrimitive 82
#define DEV_SetIndices         104
#define RING_SIZE  0x800000u
#define MAX_QUADS  16384

static IUnk* g_ring;
static u32   g_ring_pos;
static IUnk* g_quad_ib;

/* Xbox primitive -> D3D9 type and primitive count */
static u32 prim9(u32 prim, u32 count, u32* n)
{
    switch (prim) {
    case 1: *n = count; return 1;
    case 2: *n = count / 2; return 2;
    case 3: case 4: *n = count - 1; return 3;
    case 5: *n = count / 3; return 4;
    case 8: *n = count / 4 * 2; return 4;       /* quads: 2 triangles each, via g_quad_ib */
    case 6: case 9: *n = count - 2; return 5;
    default: *n = count - 2; return 6;          /* fan, polygon */
    }
}

static int ring_init(void)
{
    if (g_ring) return 1;
    if (VCALL(g_dev, DEV_CreateVertexBuffer, PFN_CreateVB)(g_dev, RING_SIZE, 0x208 /* DYNAMIC|WRITEONLY */,
                                                           0, 2 /* SYSTEMMEM */, &g_ring, NULL) < 0) return 0;
    if (VCALL(g_dev, DEV_CreateIndexBuffer, PFN_CreateIB)(g_dev, MAX_QUADS * 12, 8 /* WRITEONLY */, 101 /* INDEX16 */,
                                                          1 /* MANAGED */, &g_quad_ib, NULL) < 0) return 0;
    u16* d;
    if (VCALL(g_quad_ib, 11, PFN_VBLock)(g_quad_ib, 0, 0, (void**)&d, 0) < 0) return 0;
    for (u32 q = 0; q < MAX_QUADS; q++, d += 6) {
        u16 b = (u16)(q * 4);
        d[0] = b; d[1] = b + 1; d[2] = b + 2; d[3] = b; d[4] = b + 2; d[5] = b + 3;
    }
    VCALL(g_quad_ib, 12, PFN_VBUnlock)(g_quad_ib);
    VCALL(g_dev, DEV_SetIndices, PFN_SetObj)(g_dev, g_quad_ib);
    return 1;
}

/* copy vertices into the ring and bind them as stream s */
static int ring_stream(u32 s, const void* src, u32 bytes, u32 stride)
{
    if (bytes > RING_SIZE) return 0;
    u32 pos = (g_ring_pos + stride - 1) / stride * stride, flags = 0x1000;   /* NOOVERWRITE */
    if (pos + bytes > RING_SIZE) { pos = 0; flags = 0x2000; }                /* DISCARD */
    u8* dst;
    if (VCALL(g_ring, 11, PFN_VBLock)(g_ring, pos, bytes, (void**)&dst, flags) < 0) return 0;
    memcpy(dst, src, bytes);
    VCALL(g_ring, 12, PFN_VBUnlock)(g_ring);
    g_ring_pos = pos + bytes;
    g_vb_bytes += bytes;
    VCALL(g_dev, DEV_SetStreamSource, PFN_SetStream)(g_dev, s, g_ring, pos, stride);
    return 1;
}

static void draw9(u32 prim, u32 count)
{
    u32 n, t = prim9(prim, count, &n);
    if (!n) return;
    if (prim == 8) {
        if (count > MAX_QUADS * 4) count = MAX_QUADS * 4, n = MAX_QUADS * 2;
        VCALL(g_dev, DEV_DrawIndexedPrimitive, PFN_DrawIdx)(g_dev, 4, 0, 0, count, 0, n);
    } else
        VCALL(g_dev, DEV_DrawPrimitive, PFN_DrawPrim)(g_dev, t, 0, n);
}

/* Vertices from Xbox memory (stream data). */
static void draw_streams(u32 prim, u32 start, u32 count)
{
    if (!ring_init()) return;
    for (u32 s = 0; s < 16; s++) {
        if (!g_stream[s] || !g_stride[s]) continue;
        const u8* src = (const u8*)(g_stream[s]->Data | 0x80000000u) + start * g_stride[s];
        if (!ring_stream(s, src, count * g_stride[s], g_stride[s])) return;
    }
    draw9(prim, count);
}

/* Draw trace: env PC_TRACE=<frame>, the key T for the next frame, and once by
 * itself 300 frames after the first vertex-buffer draw (the 3D world). One line per
 * draw: kind, prim, count, stride, vertex shader / FVF, texture 0 format,
 * Z enable / func, cull, blend src / dst, stream 0 Data, first vertex. */
static char* hx(char* p, u32 v) { *p++ = ' '; for (int k = 7; k >= 0; k--) *p++ = "0123456789abcdef"[(v >> (k * 4)) & 15]; return p; }

static u32 g_trace_auto, g_trace_frame, g_shot_tag;
static void save_screenshot(void);

static void trace_draw(const char* what, u32 prim, u32 count, const void* v, u32 stride)
{
    static u32 at = 0xFFFFFFFF, key_was;
    if (at == 0xFFFFFFFF) {
        char b[16]; u32 n = GetEnvironmentVariableA("PC_TRACE", b, sizeof b);
        at = 0;
        for (u32 i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) at = at * 10 + (b[i] - '0');
    }
    u32 key = (GetAsyncKeyState('T') & 0x8000) != 0;
    if (key && !key_was) { at = g_frames + 1; log_hex("[pc] trace at frame", at); }
    key_was = key;
    if (!g_trace_auto && what[5] == 'V') { g_trace_auto = 1; at = g_frames + 300; log_hex("[pc] trace at frame", at); }
    g_trace_frame = at;
    if (!at || g_frames != at) return;
    /* the back buffer before each change of vertex shader: where the frame goes wrong */
    static u32 last_vsh = 0xFFFFFFFF;
    if (g_vshader != last_vsh && g_shot_tag < 40) { last_vsh = g_vshader; g_shot_tag++; save_screenshot(); }
    char b[256], *p = b;
    while (*what) *p++ = *what++;
    const u32* t = (const u32*)g_textures[0];
    p = hx(p, prim); p = hx(p, count); p = hx(p, stride); p = hx(p, g_vshader); p = hx(p, t ? t[3] : 0);
    p = hx(p, XRS(XRS_ZENABLE)); p = hx(p, XRS(XRS_ZFUNC)); p = hx(p, XRS(XRS_CULLMODE));
    p = hx(p, XRS(XRS_SRCBLEND)); p = hx(p, XRS(XRS_DESTBLEND));
    p = hx(p, g_stream[0] ? g_stream[0]->Data : 0);
    p = hx(p, g_pshader); p = hx(p, XTS(0, XTS_COLOROP) << 16 | XTS(0, XTS_COLORARG1) << 8 | XTS(0, XTS_COLORARG2));
    p = hx(p, XTS(1, XTS_COLOROP));
    *p++ = ' '; *p++ = '|';
    if (v) for (u32 i = 0; i < stride / 4 && i < 6; i++) p = hx(p, ((const u32*)v)[i]);
    *p++ = '\n'; *p = 0;
    log_str(b);
}

/* DrawVerticesUP(PrimitiveType, VertexCount, pVertexStreamZeroData, VertexStreamZeroStride) */
void WINAPI pc_DrawVerticesUP(u32 prim, u32 count, const void* data, u32 stride)
{
    g_stat_draw_up++;
    trace_draw("[tr] UP", prim, count, data, stride);
    if (!g_dev || !data || !count) return;
    u32 t0 = ticks();
    if (!bind_vshader()) { g_stat_skipped++; return; }
    apply_state();
    if (ring_init() && ring_stream(0, data, count * stride, stride)) draw9(prim, count);
    g_us_draw += us_since(t0);
}

/* DrawVertices(PrimitiveType, StartVertex, VertexCount) from the stream sources */
void WINAPI pc_DrawVertices(u32 prim, u32 start, u32 count)
{
    g_stat_draw++;
    trace_draw("[tr] VB", prim, count,
               g_stream[0] ? (const u8*)(g_stream[0]->Data | 0x80000000u) + start * g_stride[0] : NULL, g_stride[0]);
    if (!g_dev || !count) return;
    u32 t0 = ticks();
    if (!bind_vshader()) { g_stat_skipped++; return; }
    apply_state();
    draw_streams(prim, start, count);
    g_us_draw += us_since(t0);
}

/* ------------------------------------------------------------ screenshots */

/* F12, or the frame number in env PC_SHOT: back buffer -> shot_<frame>.bmp */

typedef HRESULT (WINAPI *PFN_GetBB)(IUnk*, u32, u32, u32, IUnk**);
typedef HRESULT (WINAPI *PFN_CreateOff)(IUnk*, u32, u32, u32, u32, IUnk**, void*);
typedef HRESULT (WINAPI *PFN_GetRTData)(IUnk*, IUnk*, IUnk*);
typedef HRESULT (WINAPI *PFN_SurfLock)(IUnk*, D3DLOCKED_RECT*, const void*, u32);
typedef HRESULT (WINAPI *PFN_SurfUnlock)(IUnk*);

static void save_screenshot(void)
{
    IUnk *bb = NULL, *off = NULL;
    VCALL(g_dev, 18, PFN_GetBB)(g_dev, 0, 0, 0, &bb);
    VCALL(g_dev, 36, PFN_CreateOff)(g_dev, g_width, g_height, D3DFMT_X8R8G8B8, 2 /* SYSTEMMEM */, &off, NULL);
    if (!bb || !off || VCALL(g_dev, 32, PFN_GetRTData)(g_dev, bb, off) < 0) { log_str("[pc] screenshot failed\n"); return; }
    D3DLOCKED_RECT lr;
    VCALL(off, 13, PFN_SurfLock)(off, &lr, NULL, 0x10 /* READONLY */);
    char name[32] = "shot_00000.bmp";
    for (u32 v = g_frames, i = 9; i >= 5; i--, v /= 10) name[i] = '0' + v % 10;
    if (g_shot_tag) { name[0] = 't'; name[1] = 'r'; name[2] = name[3] = '_'; name[4] = 'f'; name[5] = '0' + g_shot_tag / 10 % 10; name[6] = '0' + g_shot_tag % 10; }
    HANDLE f = CreateFileA(name, 0x40000000, 0, NULL, 2, 0x80, NULL);
    u32 img = g_width * g_height * 4, w;
    u8 hdr[54] = { 'B', 'M' };
    *(u32*)(hdr + 2) = 54 + img; *(u32*)(hdr + 10) = 54; *(u32*)(hdr + 14) = 40;
    *(s32*)(hdr + 18) = (s32)g_width; *(s32*)(hdr + 22) = -(s32)g_height;
    *(u16*)(hdr + 26) = 1; *(u16*)(hdr + 28) = 32; *(u32*)(hdr + 34) = img;
    WriteFile(f, hdr, 54, &w, NULL);
    for (u32 y = 0; y < g_height; y++) WriteFile(f, (u8*)lr.pBits + y * lr.Pitch, g_width * 4, &w, NULL);
    CloseHandle(f);
    VCALL(off, 14, PFN_SurfUnlock)(off);
    VCALL(off, 2, PFN_Scene)(off);
    VCALL(bb, 2, PFN_Scene)(bb);
    log_str("[pc] screenshot: "); log_str(name); log_str("\n");
}

static void maybe_screenshot(void)
{
    static int f12_was;
    static u32 at = 0xFFFFFFFF;
    if (at == 0xFFFFFFFF) {
        char b[16]; u32 n = GetEnvironmentVariableA("PC_SHOT", b, sizeof b);
        at = 0;
        for (u32 i = 0; i < n && b[i] >= '0' && b[i] <= '9'; i++) at = at * 10 + (b[i] - '0');
    }
    if (g_trace_frame && g_frames == g_trace_frame && g_shot_tag < 98) { g_shot_tag = 98; save_screenshot(); }  /* end of the traced frame */
    int f12 = (GetAsyncKeyState(0x7B) & 0x8000) != 0;
    if ((f12 && !f12_was) || (at && (g_frames == at || g_frames == at * 3 || g_frames == at * 6))) { g_shot_tag = 0; save_screenshot(); }
    f12_was = f12;
}
