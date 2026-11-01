#pragma once

#include <cstdint>
#include <cstddef>

namespace OogieEngine {
namespace Host {

// Xbox Direct3D 8 Types & Constants
enum D3DCLEAR_FLAGS {
    D3DCLEAR_TARGET  = 0x00000001,
    D3DCLEAR_ZBUFFER = 0x00000002,
    D3DCLEAR_STENCIL = 0x00000004
};

enum D3DTRANSFORMSTATETYPE {
    D3DTS_VIEW        = 2,
    D3DTS_PROJECTION  = 3,
    D3DTS_TEXTURE0    = 16,
    D3DTS_WORLD       = 256,
};

enum D3DRENDERSTATETYPE {
    D3DRS_ZENABLE           = 7,
    D3DRS_FILLMODE          = 8,
    D3DRS_SHADEMODE         = 9,
    D3DRS_ZWRITEENABLE      = 14,
    D3DRS_ALPHATESTENABLE   = 15,
    D3DRS_SRCBLEND          = 19,
    D3DRS_DESTBLEND         = 20,
    D3DRS_CULLMODE          = 22,
    D3DRS_ALPHABLENDENABLE  = 27,
    D3DRS_FOGENABLE         = 28,
};

// Direct3D 8 Host Bridge Class
class D3D8Bridge {
public:
    static D3D8Bridge& Instance();

    void Initialize();
    void HookD3DEntryPoints(uint8_t* memoryBlock, uintptr_t baseAddress);

    // D3D8 Methods called by default.exe
    static int32_t Clear(uint32_t Count, const void* pRects, uint32_t Flags, uint32_t Color, float Z, uint32_t Stencil);
    static int32_t Present(const void* pSourceRect, const void* pDestRect, void* hDestWindowOverride, const void* pDirtyRegion);
    static int32_t SetTransform(uint32_t State, const float* pMatrix);
    static int32_t GetTransform(uint32_t State, float* pMatrix);
    static int32_t SetRenderState(uint32_t State, uint32_t Value);
    static int32_t SetTexture(uint32_t Stage, void* pTexture);
    static int32_t DrawIndexedVertices(uint32_t PrimitiveType, uint32_t VertexCount, const void* pIndexData);
    static int32_t CreateDevice(uint32_t Adapter, uint32_t DeviceType, void* hFocusWindow, uint32_t BehaviorFlags, void* pPresentationParameters, void** ppReturnedDeviceInterface);

    // Frame stats
    uint64_t GetFrameCount() const { return m_frameCount; }
    bool IsWindowOpen() const { return m_isWindowOpen; }

private:
    D3D8Bridge() = default;
    ~D3D8Bridge() = default;

    uint64_t m_frameCount = 0;
    bool m_isWindowOpen = false;
    float m_worldMatrix[16] = { 0 };
    float m_viewMatrix[16] = { 0 };
    float m_projMatrix[16] = { 0 };
};

} // namespace Host
} // namespace OogieEngine
