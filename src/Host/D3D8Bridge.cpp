#include "OogieEngine/Host/D3D8Bridge.h"
#include <iostream>
#include <iomanip>
#include <cstring>

namespace OogieEngine {
namespace Host {

D3D8Bridge& D3D8Bridge::Instance() {
    static D3D8Bridge s_instance;
    return s_instance;
}

void D3D8Bridge::Initialize() {
    std::cout << "[D3D8Bridge] Initializing Modern OpenGL Graphics Bridge..." << std::endl;
    m_frameCount = 0;
    m_isWindowOpen = true;
}

static void InstallTrampoline(uint8_t* mem, uintptr_t baseAddr, uintptr_t targetVA, void* hostFunc) {
    uintptr_t offset = targetVA - baseAddr;
    uint8_t* patchSite = mem + offset;

    // x86_64 / x86 compatible absolute jump:
    // 48 B8 <64-bit address>  (mov rax, hostFunc)
    // FF E0                   (jmp rax)
    patchSite[0] = 0x48;
    patchSite[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(patchSite + 2) = reinterpret_cast<uint64_t>(hostFunc);
    patchSite[10] = 0xFF;
    patchSite[11] = 0xE0;
}

void D3D8Bridge::HookD3DEntryPoints(uint8_t* memoryBlock, uintptr_t baseAddress) {
    if (!memoryBlock) return;

    std::cout << "[D3D8Bridge] Installing JMP trampolines for Direct3D 8 functions..." << std::endl;

    // Direct3D 8 Core Functions mapped from .text disassembly:
    InstallTrampoline(memoryBlock, baseAddress, 0x001d8ee0, reinterpret_cast<void*>(&D3D8Bridge::CreateDevice));
    InstallTrampoline(memoryBlock, baseAddress, 0x001d7610, reinterpret_cast<void*>(&D3D8Bridge::Clear));
    InstallTrampoline(memoryBlock, baseAddress, 0x001d7460, reinterpret_cast<void*>(&D3D8Bridge::Present));
    InstallTrampoline(memoryBlock, baseAddress, 0x001d3550, reinterpret_cast<void*>(&D3D8Bridge::SetTransform));
    InstallTrampoline(memoryBlock, baseAddress, 0x001d3430, reinterpret_cast<void*>(&D3D8Bridge::GetTransform));
    InstallTrampoline(memoryBlock, baseAddress, 0x001d3200, reinterpret_cast<void*>(&D3D8Bridge::SetRenderState));
    InstallTrampoline(memoryBlock, baseAddress, 0x001d2e00, reinterpret_cast<void*>(&D3D8Bridge::SetTexture));
    InstallTrampoline(memoryBlock, baseAddress, 0x001d6370, reinterpret_cast<void*>(&D3D8Bridge::DrawIndexedVertices));

    std::cout << "  -> Hooked CreateDevice (0x001d8ee0)" << std::endl;
    std::cout << "  -> Hooked Clear        (0x001d7610)" << std::endl;
    std::cout << "  -> Hooked Present      (0x001d7460)" << std::endl;
    std::cout << "  -> Hooked SetTransform (0x001d3550)" << std::endl;
    std::cout << "  -> Hooked GetTransform (0x001d3430)" << std::endl;
    std::cout << "  -> Hooked SetRenderState(0x001d3200)" << std::endl;
    std::cout << "  -> Hooked SetTexture   (0x001d2e00)" << std::endl;
    std::cout << "  -> Hooked DrawIndexed  (0x001d6370)" << std::endl;
    std::cout << "[D3D8Bridge] Direct3D 8 entry points successfully routed to Host Bridge!" << std::endl;
}

int32_t D3D8Bridge::CreateDevice(uint32_t Adapter, uint32_t DeviceType, void* hFocusWindow, uint32_t BehaviorFlags, void* pPresentationParameters, void** ppReturnedDeviceInterface) {
    (void)Adapter; (void)DeviceType; (void)hFocusWindow; (void)BehaviorFlags; (void)pPresentationParameters;
    std::cout << "[D3D8Bridge] IDirect3D8::CreateDevice called by game logic!" << std::endl;

    // Return dummy valid interface pointer (or our proxy vtable)
    static uintptr_t s_dummyDeviceVTable[256];
    static uintptr_t s_dummyDeviceInstance = reinterpret_cast<uintptr_t>(s_dummyDeviceVTable);

    if (ppReturnedDeviceInterface) {
        *ppReturnedDeviceInterface = &s_dummyDeviceInstance;
    }

    return 0; // D3D_OK
}

int32_t D3D8Bridge::Clear(uint32_t Count, const void* pRects, uint32_t Flags, uint32_t Color, float Z, uint32_t Stencil) {
    (void)Count; (void)pRects; (void)Z; (void)Stencil;
    float a = ((Color >> 24) & 0xFF) / 255.0f;
    float r = ((Color >> 16) & 0xFF) / 255.0f;
    float g = ((Color >> 8) & 0xFF) / 255.0f;
    float b = (Color & 0xFF) / 255.0f;

    // Log first few clears
    static int s_clearLogs = 0;
    if (s_clearLogs++ < 3) {
        std::cout << "[D3D8Bridge] Clear(Flags=0x" << std::hex << Flags
                  << ", RGBA=(" << r << "," << g << "," << b << "," << a << "))" << std::dec << std::endl;
    }

    return 0; // D3D_OK
}

int32_t D3D8Bridge::Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, const void* pDirtyRegion) {
    (void)pSourceRect; (void)pDestRect; (void)hDestWindowOverride; (void)pDirtyRegion;
    auto& inst = Instance();
    inst.m_frameCount++;

    if (inst.m_frameCount % 60 == 0) {
        std::cout << "[D3D8Bridge] Frame " << inst.m_frameCount
                  << " presented successfully (Modern Render Target)" << std::endl;
    }

    return 0; // D3D_OK
}

int32_t D3D8Bridge::SetTransform(uint32_t State, const float* pMatrix) {
    auto& inst = Instance();
    if (!pMatrix) return 0;

    if (State == D3DTS_WORLD) {
        std::memcpy(inst.m_worldMatrix, pMatrix, 16 * sizeof(float));
    } else if (State == D3DTS_VIEW) {
        std::memcpy(inst.m_viewMatrix, pMatrix, 16 * sizeof(float));
    } else if (State == D3DTS_PROJECTION) {
        std::memcpy(inst.m_projMatrix, pMatrix, 16 * sizeof(float));
    }

    return 0; // D3D_OK
}

int32_t D3D8Bridge::GetTransform(uint32_t State, float* pMatrix) {
    auto& inst = Instance();
    if (!pMatrix) return 0;

    if (State == D3DTS_WORLD) {
        std::memcpy(pMatrix, inst.m_worldMatrix, 16 * sizeof(float));
    } else if (State == D3DTS_VIEW) {
        std::memcpy(pMatrix, inst.m_viewMatrix, 16 * sizeof(float));
    } else if (State == D3DTS_PROJECTION) {
        std::memcpy(pMatrix, inst.m_projMatrix, 16 * sizeof(float));
    } else {
        std::memset(pMatrix, 0, 16 * sizeof(float));
        pMatrix[0] = pMatrix[5] = pMatrix[10] = pMatrix[15] = 1.0f; // Identity
    }

    return 0; // D3D_OK
}

int32_t D3D8Bridge::SetRenderState(uint32_t State, uint32_t Value) {
    // Intercept render states
    static int s_rsLogs = 0;
    if (s_rsLogs++ < 5) {
        std::cout << "[D3D8Bridge] SetRenderState(State=" << State << ", Val=" << Value << ")" << std::endl;
    }
    return 0; // D3D_OK
}

int32_t D3D8Bridge::SetTexture(uint32_t Stage, void* pTexture) {
    (void)Stage; (void)pTexture;
    return 0; // D3D_OK
}

int32_t D3D8Bridge::DrawIndexedVertices(uint32_t PrimitiveType, uint32_t VertexCount, const void* pIndexData) {
    (void)PrimitiveType; (void)VertexCount; (void)pIndexData;
    static int s_drawLogs = 0;
    if (s_drawLogs++ < 5) {
        std::cout << "[D3D8Bridge] DrawIndexedVertices(PrimType=" << PrimitiveType
                  << ", Verts=" << VertexCount << ")" << std::endl;
    }
    return 0; // D3D_OK
}

} // namespace Host
} // namespace OogieEngine

