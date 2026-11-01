/*
 * Minimal Win32 + Direct3D 9 declarations for the in-exe PC renderer.
 * Built freestanding: clang --target=i686-pc-windows-gnu, no SDK headers.
 */
#pragma once

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed int         s32;
typedef u32                size_t;
typedef void*              HANDLE;
typedef void*              HWND;
typedef void*              HINSTANCE;
typedef int                BOOL;
typedef s32                HRESULT;

#define WINAPI   __attribute__((stdcall))
#define FASTCALL __attribute__((fastcall))
#define IMPORT   __attribute__((dllimport))
#define NULL     ((void*)0)

/* ---- kernel32 -------------------------------------------------------- */
IMPORT void*  WINAPI VirtualAlloc(void* p, size_t n, u32 type, u32 prot);
IMPORT BOOL   WINAPI VirtualFree(void* p, size_t n, u32 type);
IMPORT HANDLE WINAPI GetModuleHandleA(const char* name);
IMPORT void   WINAPI ExitProcess(u32 code);
IMPORT void   WINAPI OutputDebugStringA(const char* s);
IMPORT HANDLE WINAPI GetStdHandle(u32 n);
IMPORT BOOL   WINAPI WriteFile(HANDLE h, const void* p, u32 n, u32* done, void* ov);
IMPORT void   WINAPI Sleep(u32 ms);

#define MEM_COMMIT     0x1000
#define MEM_RESERVE    0x2000
#define MEM_RELEASE    0x8000
#define PAGE_READWRITE 0x04

/* ---- user32 ---------------------------------------------------------- */
typedef s32 (WINAPI *WNDPROC)(HWND, u32, u32, s32);
typedef struct { u32 style; WNDPROC proc; int cls, wnd; HINSTANCE inst; void* icon; void* cursor;
                 void* bg; const char* menu; const char* name; } WNDCLASSA;
typedef struct { HWND hwnd; u32 msg; u32 wp; s32 lp; u32 time; s32 x, y; } MSG;
typedef struct { s32 left, top, right, bottom; } RECT;

IMPORT u16  WINAPI RegisterClassA(const WNDCLASSA* wc);
IMPORT HWND WINAPI CreateWindowExA(u32 ex, const char* cls, const char* title, u32 style, int x, int y,
                                   int w, int h, HWND parent, void* menu, HINSTANCE inst, void* param);
IMPORT BOOL WINAPI ShowWindow(HWND h, int cmd);
IMPORT BOOL WINAPI PeekMessageA(MSG* m, HWND h, u32 lo, u32 hi, u32 remove);
IMPORT BOOL WINAPI TranslateMessage(const MSG* m);
IMPORT s32  WINAPI DispatchMessageA(const MSG* m);
IMPORT s32  WINAPI DefWindowProcA(HWND h, u32 msg, u32 wp, s32 lp);
IMPORT void WINAPI PostQuitMessage(int code);
IMPORT BOOL WINAPI AdjustWindowRect(RECT* r, u32 style, BOOL menu);
IMPORT void* WINAPI LoadCursorA(HINSTANCE inst, const char* name);

#define WS_OVERLAPPEDWINDOW 0x00CF0000
#define WS_VISIBLE          0x10000000
#define CW_USEDEFAULT       ((int)0x80000000)
#define WM_DESTROY 0x0002
#define WM_CLOSE   0x0010
#define WM_QUIT    0x0012
#define PM_REMOVE  1
#define SW_SHOW    5

/* ---- Direct3D 9 (COM through raw vtables) ------------------------------ */
typedef struct { void** vt; } IUnk;

IMPORT IUnk* WINAPI Direct3DCreate9(u32 sdk);

#define D3D_SDK_VERSION 32
#define VCALL(obj, idx, T) ((T)((obj)->vt[idx]))

typedef struct {
    u32 BackBufferWidth, BackBufferHeight, BackBufferFormat, BackBufferCount;
    u32 MultiSampleType, MultiSampleQuality, SwapEffect;
    HWND hDeviceWindow;
    BOOL Windowed, EnableAutoDepthStencil;
    u32 AutoDepthStencilFormat, Flags, FullScreen_RefreshRateInHz, PresentationInterval;
} D3DPRESENT_PARAMETERS9;

typedef struct { u32 X, Y, Width, Height; float MinZ, MaxZ; } D3DVIEWPORT;
typedef struct { s32 Pitch; void* pBits; } D3DLOCKED_RECT;

/* IDirect3D9 */
#define D3D9_CreateDevice 16
/* IDirect3DDevice9 */
#define DEV_Release            2
#define DEV_Present           17
#define DEV_CreateTexture     23
#define DEV_BeginScene        41
#define DEV_EndScene          42
#define DEV_Clear             43
#define DEV_SetTransform      44
#define DEV_SetViewport       47
#define DEV_SetRenderState    57
#define DEV_SetTexture        65
#define DEV_SetTextureStageState 67
#define DEV_SetSamplerState   69
#define DEV_DrawPrimitiveUP   83
#define DEV_SetFVF            89

#define D3DDEVTYPE_HAL 1
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x40
#define D3DCREATE_FPU_PRESERVE              0x02
#define D3DCREATE_MULTITHREADED             0x04
#define D3DSWAPEFFECT_DISCARD 1
#define D3DFMT_X8R8G8B8 22
#define D3DFMT_D24S8    75
#define D3DPRESENT_INTERVAL_ONE 1
#define D3DCLEAR_TARGET  1
#define D3DCLEAR_ZBUFFER 2
#define D3DCLEAR_STENCIL 4
