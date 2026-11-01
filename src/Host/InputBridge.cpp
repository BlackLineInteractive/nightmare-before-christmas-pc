#include "OogieEngine/Host/InputBridge.h"
#include <iostream>
#include <cstring>

namespace OogieEngine {
namespace Host {

InputBridge& InputBridge::Instance() {
    static InputBridge s_instance;
    return s_instance;
}

void InputBridge::Initialize() {
    std::cout << "[InputBridge] Initializing Modern Gamepad & Keyboard Input Bridge..." << std::endl;
    std::memset(m_state, 0, sizeof(m_state));
    m_packetNumber = 1;
}

static void InstallTrampoline(uint8_t* mem, uintptr_t baseAddr, uintptr_t targetVA, void* hostFunc) {
    uintptr_t offset = targetVA - baseAddr;
    uint8_t* patchSite = mem + offset;

    patchSite[0] = 0x48;
    patchSite[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(patchSite + 2) = reinterpret_cast<uint64_t>(hostFunc);
    patchSite[10] = 0xFF;
    patchSite[11] = 0xE0;
}

void InputBridge::HookXPPEntryPoints(uint8_t* memoryBlock, uintptr_t baseAddress) {
    if (!memoryBlock) return;

    std::cout << "[InputBridge] Installing JMP trampolines for XPP Gamepad functions..." << std::endl;

    // Top XPP functions identified in .text disassembly:
    InstallTrampoline(memoryBlock, baseAddress, 0x0025986b, reinterpret_cast<void*>(&InputBridge::XInitDevices));
    InstallTrampoline(memoryBlock, baseAddress, 0x002598cd, reinterpret_cast<void*>(&InputBridge::XGetDevices));
    InstallTrampoline(memoryBlock, baseAddress, 0x002598c1, reinterpret_cast<void*>(&InputBridge::XInputOpen));
    InstallTrampoline(memoryBlock, baseAddress, 0x00259b72, reinterpret_cast<void*>(&InputBridge::XInputClose));
    InstallTrampoline(memoryBlock, baseAddress, 0x00259b94, reinterpret_cast<void*>(&InputBridge::XInputGetState));
    InstallTrampoline(memoryBlock, baseAddress, 0x00259aa5, reinterpret_cast<void*>(&InputBridge::XInputSetState));
    InstallTrampoline(memoryBlock, baseAddress, 0x00259b18, reinterpret_cast<void*>(&InputBridge::XInputPoll));

    std::cout << "  -> Hooked XInitDevices (0x0025986b)" << std::endl;
    std::cout << "  -> Hooked XGetDevices  (0x002598cd)" << std::endl;
    std::cout << "  -> Hooked XInputOpen   (0x002598c1)" << std::endl;
    std::cout << "  -> Hooked XInputClose  (0x00259b72)" << std::endl;
    std::cout << "  -> Hooked XInputGetState(0x00259b94)" << std::endl;
    std::cout << "  -> Hooked XInputSetState(0x00259aa5)" << std::endl;
    std::cout << "  -> Hooked XInputPoll   (0x00259b18)" << std::endl;
    std::cout << "[InputBridge] XPP Gamepad entry points successfully routed to Host Input!" << std::endl;
}

int32_t InputBridge::XInitDevices(uint32_t dwNumDeviceTypes, void* pDeviceTypes) {
    (void)dwNumDeviceTypes; (void)pDeviceTypes;
    std::cout << "[InputBridge] XInitDevices called: Controller Port 0 connected (Modern Gamepad/Keyboard)." << std::endl;
    return 0; // SUCCESS
}

uint32_t InputBridge::XGetDevices(uint32_t dwDeviceType) {
    (void)dwDeviceType;
    // Bit 0 = Port 0 inserted
    return 0x00000001;
}

int32_t InputBridge::XInputOpen(uint32_t dwDeviceType, uint32_t dwPort, uint32_t dwSlot, void* pDescription, void** ppDevice) {
    (void)dwDeviceType; (void)dwPort; (void)dwSlot; (void)pDescription;
    static uint32_t s_dummyDeviceHandle = 0x5001;
    if (ppDevice) {
        *ppDevice = &s_dummyDeviceHandle;
    }
    return 0; // ERROR_SUCCESS
}

void InputBridge::XInputClose(void* hDevice) {
    (void)hDevice;
}

int32_t InputBridge::XInputGetState(void* hDevice, XINPUT_STATE* pState) {
    (void)hDevice;
    if (!pState) return 1167; // ERROR_DEVICE_NOT_CONNECTED

    auto& inst = Instance();
    inst.m_packetNumber++;
    inst.m_state[0].dwPacketNumber = inst.m_packetNumber;

    *pState = inst.m_state[0];
    return 0; // ERROR_SUCCESS
}

int32_t InputBridge::XInputSetState(void* hDevice, XINPUT_FEEDBACK* pFeedback) {
    (void)hDevice;
    if (pFeedback) {
        // Rumble feedback (can be wired to modern DualSense / Xbox controller rumble)
    }
    return 0; // ERROR_SUCCESS
}

int32_t InputBridge::XInputPoll(void* hDevice) {
    (void)hDevice;
    return 0; // ERROR_SUCCESS
}

void InputBridge::UpdateKeyState(int key, bool isDown) {
    // Map keyboard keys to Xbox gamepad controls:
    // W/A/S/D -> Left Thumbstick
    // Space   -> A Button (Jump)
    // J       -> X Button (Soul Robber)
    // K       -> B Button (Attack)
    // L       -> Y Button (Costume Change)
    // Esc     -> Start Button (Pause)
    if (key == 'W') m_state[0].Gamepad.sThumbLY = isDown ? 32767 : 0;
    if (key == 'S') m_state[0].Gamepad.sThumbLY = isDown ? -32768 : 0;
    if (key == 'A') m_state[0].Gamepad.sThumbLX = isDown ? -32768 : 0;
    if (key == 'D') m_state[0].Gamepad.sThumbLX = isDown ? 32767 : 0;

    if (key == ' ') m_state[0].Gamepad.bAnalogButtons[0] = isDown ? 255 : 0; // A
    if (key == 'K') m_state[0].Gamepad.bAnalogButtons[1] = isDown ? 255 : 0; // B
    if (key == 'J') m_state[0].Gamepad.bAnalogButtons[2] = isDown ? 255 : 0; // X
    if (key == 'L') m_state[0].Gamepad.bAnalogButtons[3] = isDown ? 255 : 0; // Y

    if (key == 27) { // Esc
        if (isDown) m_state[0].Gamepad.wButtons |= XBOX_START;
        else m_state[0].Gamepad.wButtons &= ~XBOX_START;
    }
}

void InputBridge::UpdateMouseDelta(float dx, float dy) {
    // Map mouse movement to Right Thumbstick (Camera Look)
    m_state[0].Gamepad.sThumbRX = static_cast<int16_t>(dx * 300.0f);
    m_state[0].Gamepad.sThumbRY = static_cast<int16_t>(-dy * 300.0f);
}

} // namespace Host
} // namespace OogieEngine

