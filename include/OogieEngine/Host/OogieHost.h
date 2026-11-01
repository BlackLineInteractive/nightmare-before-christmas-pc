#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace OogieEngine {
namespace Host {

struct PESectionInfo {
    std::string name;
    uint32_t virtualAddress = 0;
    uint32_t virtualSize = 0;
    uint32_t rawAddress = 0;
    uint32_t rawSize = 0;
    uint32_t characteristics = 0;
};

class OogieHost {
public:
    static OogieHost& Instance();

    bool Initialize(const std::string& pePath = "rom/extracted/default.exe");
    bool LoadBinary();
    bool PatchKernelThunks();
    bool PatchHardwareDirect3D();
    bool PatchGamepadInput();
    void Shutdown();

    uintptr_t GetBaseAddress() const { return m_baseAddress; }
    uintptr_t GetEntryPoint() const { return m_entryPoint; }
    bool IsLoaded() const { return m_isLoaded; }

    const std::vector<PESectionInfo>& GetSections() const { return m_sections; }

private:
    OogieHost() = default;
    ~OogieHost();

    std::string m_pePath;
    uintptr_t m_baseAddress = 0x00010000;
    uintptr_t m_entryPoint = 0x001bf647;
    uint32_t m_imageSize = 0x00431000;
    uint8_t* m_memoryBlock = nullptr;
    size_t m_memoryBlockSize = 64 * 1024 * 1024; // 64 MB Xbox RAM
    bool m_isLoaded = false;

    std::vector<PESectionInfo> m_sections;
};

} // namespace Host
} // namespace OogieEngine

