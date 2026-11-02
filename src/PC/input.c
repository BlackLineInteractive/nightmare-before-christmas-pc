/*
 * PC input for default.xbe: replaces the Xbox XPP (USB gamepad) library.
 * Port 0 reads a Windows XInput gamepad and the keyboard; other ports are empty.
 *
 * Keyboard: WASD = left stick, IJKL = right stick, arrows = d-pad,
 *           Space = A, X = B, C = X, V = Y, Q / E = left / right trigger,
 *           1 = white, 2 = black, Enter = Start, Esc = Back,
 *           F = left stick click, H = right stick click.
 */
#include "win32.h"

IMPORT u32 WINAPI XInputGetState(u32 index, void* state);    /* xinput9_1_0.dll */

/* Xbox XINPUT_GAMEPAD */
typedef struct {
    u16 wButtons;
    u8  bAnalogButtons[8];   /* A, B, X, Y, black, white, left trigger, right trigger */
    short sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XBOX_GAMEPAD;
typedef struct { u32 dwPacketNumber; XBOX_GAMEPAD Gamepad; } XBOX_INPUT_STATE;

/* Windows XINPUT_STATE */
typedef struct {
    u32 dwPacketNumber;
    u16 wButtons; u8 bLeftTrigger, bRightTrigger;
    short sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} WIN_INPUT_STATE;

static u32 g_packet;
static int g_reported;

static int key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

/* XInitDevices(dwPreallocTypeCount, PreallocTypes) */
void WINAPI pc_XInitDevices(u32 n, const void* types) { }

/* XGetDevices(DeviceType) -> bitmask of occupied ports */
u32 WINAPI pc_XGetDevices(const void* type) { return 1; }

/* XGetDeviceChanges(DeviceType, pInsertions, pRemovals) */
BOOL WINAPI pc_XGetDeviceChanges(const void* type, u32* ins, u32* rem)
{
    *rem = 0;
    *ins = g_reported ? 0 : 1;
    if (g_reported) return 0;
    g_reported = 1;
    return 1;
}

/* XInputOpen(DeviceType, dwPort, dwSlot, pPollingParameters) -> HANDLE */
HANDLE WINAPI pc_XInputOpen(const void* type, u32 port, u32 slot, const void* poll)
{
    return port == 0 ? (HANDLE)1 : NULL;
}

void WINAPI pc_XInputClose(HANDLE h) { }

/* XInputGetCapabilities(hDevice, pCapabilities): SubType, Reserved, In.Gamepad, Out.Rumble */
u32 WINAPI pc_XInputGetCapabilities(HANDLE h, u8* caps)
{
    memset(caps, 0, 2 + sizeof(XBOX_GAMEPAD) + 4);
    caps[0] = 1;                                    /* XINPUT_DEVSUBTYPE_GC_GAMEPAD */
    memset(caps + 2, 0xFF, sizeof(XBOX_GAMEPAD));   /* every control present */
    return 0;
}

static short axis(int neg, int pos) { return (short)(pos ? 32767 : 0) - (short)(neg ? 32767 : 0); }
static u8 analog(int down) { return down ? 255 : 0; }

/* Scripted input for testing, env PC_KEYS="<frame>:<buttons>,...": from that
 * frame the buttons are held for 8 frames. Buttons: S start, K back, A B X Y,
 * U D L R (d-pad), P = take a screenshot then. e.g. PC_KEYS=2400:S,2600:A */
static struct { u32 frame; char b[6]; } g_keys[64];
static u32 g_nkeys = 0xFFFFFFFF;

static void scripted_load(void)
{
    if (g_nkeys == 0xFFFFFFFF) {
        static char e[512];
        u32 n = GetEnvironmentVariableA("PC_KEYS", e, sizeof e - 1), i = 0;
        g_nkeys = 0;
        while (i < n && g_nkeys < 64) {
            u32 f = 0, k = 0;
            while (i < n && e[i] >= '0' && e[i] <= '9') f = f * 10 + (e[i++] - '0');
            if (i < n && e[i] == ':') i++;
            while (i < n && e[i] != ',' && k < 5) g_keys[g_nkeys].b[k++] = e[i++];
            while (i < n && e[i] != ',') i++;
            i++;
            g_keys[g_nkeys++].frame = f;
        }
        if (g_nkeys) log_hex("[pc] PC_KEYS entries", g_nkeys);
    }
}

/* P entries: called from Swap, so they also fire while the game reads no input */
static void scripted_shots(void)
{
    scripted_load();
    for (u32 i = 0; i < g_nkeys; i++)
        if (g_frames == g_keys[i].frame)
            for (const char* c = g_keys[i].b; *c; c++)
                if (*c == 'P') save_screenshot();
}

static void scripted(XBOX_GAMEPAD* g)
{
    scripted_load();
    for (u32 i = 0; i < g_nkeys; i++) {
        if (g_frames < g_keys[i].frame || g_frames >= g_keys[i].frame + 8) continue;
        for (const char* c = g_keys[i].b; *c; c++)
            switch (*c) {
            case 'S': g->wButtons |= 0x10; break;
            case 'K': g->wButtons |= 0x20; break;
            case 'U': g->wButtons |= 0x01; break;
            case 'D': g->wButtons |= 0x02; break;
            case 'L': g->wButtons |= 0x04; break;
            case 'R': g->wButtons |= 0x08; break;
            case 'A': g->bAnalogButtons[0] = 255; break;
            case 'B': g->bAnalogButtons[1] = 255; break;
            case 'X': g->bAnalogButtons[2] = 255; break;
            case 'Y': g->bAnalogButtons[3] = 255; break;
            }
    }
}

/* XInputGetState(hDevice, pState) */
u32 WINAPI pc_XInputGetState(HANDLE h, XBOX_INPUT_STATE* st)
{
    XBOX_GAMEPAD* g = &st->Gamepad;
    memset(g, 0, sizeof(*g));

    WIN_INPUT_STATE w;
    if (XInputGetState(0, &w) == 0) {
        g->wButtons = w.wButtons & 0xFF;            /* d-pad, start, back, thumbs: same bits */
        g->bAnalogButtons[0] = analog(w.wButtons & 0x1000);   /* A */
        g->bAnalogButtons[1] = analog(w.wButtons & 0x2000);   /* B */
        g->bAnalogButtons[2] = analog(w.wButtons & 0x4000);   /* X */
        g->bAnalogButtons[3] = analog(w.wButtons & 0x8000);   /* Y */
        g->bAnalogButtons[4] = analog(w.wButtons & 0x0200);   /* black = RB */
        g->bAnalogButtons[5] = analog(w.wButtons & 0x0100);   /* white = LB */
        g->bAnalogButtons[6] = w.bLeftTrigger;
        g->bAnalogButtons[7] = w.bRightTrigger;
        g->sThumbLX = w.sThumbLX; g->sThumbLY = w.sThumbLY;
        g->sThumbRX = w.sThumbRX; g->sThumbRY = w.sThumbRY;
    }

    if (key(0x26)) g->wButtons |= 0x01;   /* up */
    if (key(0x28)) g->wButtons |= 0x02;   /* down */
    if (key(0x25)) g->wButtons |= 0x04;   /* left */
    if (key(0x27)) g->wButtons |= 0x08;   /* right */
    if (key(0x0D)) g->wButtons |= 0x10;   /* Enter: start */
    if (key(0x1B)) g->wButtons |= 0x20;   /* Esc: back */
    if (key('F'))  g->wButtons |= 0x40;
    if (key('H'))  g->wButtons |= 0x80;
    if (key(0x20)) g->bAnalogButtons[0] = 255;
    if (key('X'))  g->bAnalogButtons[1] = 255;
    if (key('C'))  g->bAnalogButtons[2] = 255;
    if (key('V'))  g->bAnalogButtons[3] = 255;
    if (key('2'))  g->bAnalogButtons[4] = 255;
    if (key('1'))  g->bAnalogButtons[5] = 255;
    if (key('Q'))  g->bAnalogButtons[6] = 255;
    if (key('E'))  g->bAnalogButtons[7] = 255;
    if (key('A') || key('D')) g->sThumbLX = axis(key('A'), key('D'));
    if (key('W') || key('S')) g->sThumbLY = axis(key('S'), key('W'));
    if (key('J') || key('L')) g->sThumbRX = axis(key('J'), key('L'));
    if (key('I') || key('K')) g->sThumbRY = axis(key('K'), key('I'));

    scripted(g);
    st->dwPacketNumber = ++g_packet;
    return 0;
}

/* XInputSetState(hDevice, pFeedback): rumble; completes at once */
u32 WINAPI pc_XInputSetState(HANDLE h, u32* feedback)
{
    feedback[0] = 0;   /* Header.dwStatus = ERROR_SUCCESS */
    return 0;
}
