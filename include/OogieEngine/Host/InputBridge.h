#pragma once

#include <cstdint>

namespace OogieEngine {
namespace Host {

#pragma pack(push, 1)
struct XINPUT_GAMEPAD {
    uint16_t wButtons;
    uint8_t  bAnalogButtons[8]; // A, B, X, Y, BLACK, WHITE, LTRIGGER, RTRIGGER
    int16_t  sThumbLX;
    int16_t  sThumbLY;
    int16_t  sThumbRX;
    int16_t  sThumbRY;
};

struct XINPUT_STATE {
    uint32_t dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
};

struct XINPUT_FEEDBACK {
    struct {
        uint16_t wLeftMotorSpeed;
        uint16_t wRightMotorSpeed;
    } Rumble;
};
#pragma pack(pop)

// Xbox Button Bitmasks
enum XBOX_BUTTONS {
    XBOX_DPAD_UP        = 0x0001,
    XBOX_DPAD_DOWN      = 0x0002,
    XBOX_DPAD_LEFT      = 0x0004,
    XBOX_DPAD_RIGHT     = 0x0008,
    XBOX_START          = 0x0010,
    XBOX_BACK           = 0x0020,
    XBOX_LEFT_THUMB     = 0x0040,
    XBOX_RIGHT_THUMB    = 0x0080,
};

class InputBridge {
public:
    static InputBridge& Instance();

    void Initialize();
    void HookXPPEntryPoints(uint8_t* memoryBlock, uintptr_t baseAddress);

    // XPP HLE Callbacks
    static int32_t XInitDevices(uint32_t dwNumDeviceTypes, void* pDeviceTypes);
    static uint32_t XGetDevices(uint32_t dwDeviceType);
    static int32_t XInputOpen(uint32_t dwDeviceType, uint32_t dwPort, uint32_t dwSlot, void* pDescription, void** ppDevice);
    static void XInputClose(void* hDevice);
    static int32_t XInputGetState(void* hDevice, XINPUT_STATE* pState);
    static int32_t XInputSetState(void* hDevice, XINPUT_FEEDBACK* pFeedback);
    static int32_t XInputPoll(void* hDevice);

    // Host updates
    void UpdateKeyState(int key, bool isDown);
    void UpdateMouseDelta(float dx, float dy);

private:
    InputBridge() = default;
    ~InputBridge() = default;

    XINPUT_STATE m_state[4] = {};
    uint32_t m_packetNumber = 0;
};

} // namespace Host
} // namespace OogieEngine

