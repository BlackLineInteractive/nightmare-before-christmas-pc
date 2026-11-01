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

#define XRS(i)      (((volatile u32*)0x001e2a94)[i])            /* D3D__RenderState */
#define XTS(s, i)   (((volatile u32*)0x001e2890)[(s) * 32 + (i)]) /* D3D__TextureState */

/* Xbox D3DRENDERSTATETYPE (XDK 5849) */
enum { XRS_ZFUNC = 57, XRS_ALPHAFUNC, XRS_ALPHABLENDENABLE, XRS_ALPHATESTENABLE, XRS_ALPHAREF,
       XRS_SRCBLEND, XRS_DESTBLEND, XRS_ZWRITEENABLE, XRS_DITHERENABLE, XRS_SHADEMODE,
       XRS_COLORWRITEENABLE, XRS_BLENDOP = 74, XRS_FOGENABLE = 92, XRS_LIGHTING = 102,
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
typedef HRESULT (WINAPI *PFN_SetFVF)(IUnk*, u32);
typedef HRESULT (WINAPI *PFN_SetXf)(IUnk*, u32, const float*);
typedef HRESULT (WINAPI *PFN_DrawUP)(IUnk*, u32, u32, const void*, u32);
typedef HRESULT (WINAPI *PFN_DrawIdxUP)(IUnk*, u32, u32, u32, u32, const void*, u32, const void*, u32);
typedef HRESULT (WINAPI *PFN_CreateTex)(IUnk*, u32, u32, u32, u32, u32, u32, IUnk**, void*);
typedef HRESULT (WINAPI *PFN_TexLock)(IUnk*, u32, D3DLOCKED_RECT*, const void*, u32);
typedef HRESULT (WINAPI *PFN_TexUnlock)(IUnk*, u32);

#define DEV_DrawIndexedPrimitiveUP 84
#define TEX_LockRect   19
#define TEX_UnlockRect 20

static void rs(u32 s, u32 v) { VCALL(g_dev, DEV_SetRenderState, PFN_SetRS)(g_dev, s, v); }
static void tss(u32 st, u32 s, u32 v) { VCALL(g_dev, DEV_SetTextureStageState, PFN_SetTSS)(g_dev, st, s, v); }
static void samp(u32 st, u32 s, u32 v) { VCALL(g_dev, DEV_SetSamplerState, PFN_SetTSS)(g_dev, st, s, v); }

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
            e->tex = upload_texture(res);
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

static void apply_state(void)
{
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

    for (u32 s = 0; s < 4; s++) {
        IUnk* t = g_textures[s] ? get_texture((const u32*)g_textures[s]) : NULL;
        VCALL(g_dev, DEV_SetTexture, PFN_SetTex)(g_dev, s, t);
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
        if (cop == 1) break;                 /* later stages are off too */
    }

    /* Xbox D3DTS: VIEW 0, PROJECTION 1, TEXTURE0..3 2..5, WORLD 6 */
    PFN_SetXf setxf = VCALL(g_dev, DEV_SetTransform, PFN_SetXf);
    setxf(g_dev, 2, g_transforms[0]);
    setxf(g_dev, 3, g_transforms[1]);
    setxf(g_dev, 256, g_transforms[6]);
    for (u32 s = 0; s < 4; s++) setxf(g_dev, 16 + s, g_transforms[2 + s]);
}

/* ------------------------------------------------------------------- draw */

/* Xbox primitive types: 1 points, 2 lines, 3 line loop, 4 line strip,
 * 5 triangles, 6 strip, 7 fan, 8 quads, 9 quad strip, 10 polygon */
static void draw(u32 prim, u32 count, const u8* v, u32 stride)
{
    if (!g_dev || !v || !count) return;
    if (g_vshader & 1) { g_stat_skipped++; return; }    /* programmable vertex shader: later */
    VCALL(g_dev, DEV_SetFVF, PFN_SetFVF)(g_dev, g_vshader);
    apply_state();

    PFN_DrawUP up = VCALL(g_dev, DEV_DrawPrimitiveUP, PFN_DrawUP);
    switch (prim) {
    case 1: up(g_dev, 1, count, v, stride); break;
    case 2: up(g_dev, 2, count / 2, v, stride); break;
    case 3: case 4: up(g_dev, 3, count - 1, v, stride); break;
    case 5: up(g_dev, 4, count / 3, v, stride); break;
    case 6: case 9: up(g_dev, 5, count - 2, v, stride); break;
    case 7: case 10: up(g_dev, 6, count - 2, v, stride); break;
    case 8: {                                            /* quads -> indexed triangles */
        static u16 idx[6 * 4096];
        u32 quads = count / 4;
        if (quads > 4096) quads = 4096;
        for (u32 q = 0; q < quads; q++) {
            u16 b = (u16)(q * 4);
            idx[q * 6 + 0] = b; idx[q * 6 + 1] = b + 1; idx[q * 6 + 2] = b + 2;
            idx[q * 6 + 3] = b; idx[q * 6 + 4] = b + 2; idx[q * 6 + 5] = b + 3;
        }
        VCALL(g_dev, DEV_DrawIndexedPrimitiveUP, PFN_DrawIdxUP)(g_dev, 4, 0, quads * 4, quads * 2, idx,
                                                                101 /* INDEX16 */, v, stride);
        break;
    }
    }
}

/* DrawVerticesUP(PrimitiveType, VertexCount, pVertexStreamZeroData, VertexStreamZeroStride) */
void WINAPI pc_DrawVerticesUP(u32 prim, u32 count, const void* data, u32 stride)
{
    g_stat_draw_up++;
    draw(prim, count, data, stride);
}

/* DrawVertices(PrimitiveType, StartVertex, VertexCount) from stream 0 */
void WINAPI pc_DrawVertices(u32 prim, u32 start, u32 count)
{
    g_stat_draw++;
    if (!g_stream0) return;
    const u8* base = (const u8*)(g_stream0->Data | 0x80000000u);
    draw(prim, count, base + start * g_stride0, g_stride0);
}

/* ------------------------------------------------------------ screenshots */

/* F12, or the frame number in env PC_SHOT: back buffer -> shot_<frame>.bmp */
IMPORT u32    WINAPI GetEnvironmentVariableA(const char* name, char* buf, u32 n);
IMPORT HANDLE WINAPI CreateFileA(const char* name, u32 access, u32 share, void* sa, u32 disp, u32 flags, HANDLE tmpl);
IMPORT BOOL   WINAPI CloseHandle(HANDLE h);

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
    int f12 = (GetAsyncKeyState(0x7B) & 0x8000) != 0;
    if ((f12 && !f12_was) || (at && g_frames == at)) save_screenshot();
    f12_was = f12;
}
