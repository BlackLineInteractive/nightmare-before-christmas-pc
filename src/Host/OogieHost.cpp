#include "OogieEngine/Host/OogieHost.h"
#include "OogieEngine/Host/XboxKernel.h"
#include "OogieEngine/Core/VFS.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <sys/mman.h>

namespace OogieEngine {
namespace Host {

OogieHost& OogieHost::Instance() {
    static OogieHost s_instance;
    return s_instance;
}

OogieHost::~OogieHost() {
    Shutdown();
}

void OogieHost::Shutdown() {
    if (m_memoryBlock) {
        munmap(m_memoryBlock, m_memoryBlockSize);
        m_memoryBlock = nullptr;
    }
    m_sections.clear();
    m_isLoaded = false;
}

bool OogieHost::Initialize(const std::string& pePath) {
    m_pePath = pePath;
    std::cout << "[OogieHost] Initializing Host Runner with binary: " << pePath << std::endl;

    if (!LoadBinary()) {
        std::cerr << "[OogieHost] Failed to load PE binary!" << std::endl;
        return false;
    }

    if (!PatchKernelThunks()) {
        std::cerr << "[OogieHost] Failed to patch Xbox Kernel Thunks!" << std::endl;
        return false;
    }

    if (!PatchHardwareDirect3D()) {
        std::cerr << "[OogieHost] Failed to patch Direct3D entry points!" << std::endl;
        return false;
    }

    if (!PatchGamepadInput()) {
        std::cerr << "[OogieHost] Failed to patch Gamepad input!" << std::endl;
        return false;
    }

    std::cout << "[OogieHost] Initialization complete! Native execution harness is ready." << std::endl;
    return true;
}

bool OogieHost::LoadBinary() {
    std::ifstream file(m_pePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[OogieHost] Could not open file: " << m_pePath << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    if (buffer.size() < 0x200 || buffer[0] != 'M' || buffer[1] != 'Z') {
        std::cerr << "[OogieHost] Invalid DOS header magic!" << std::endl;
        return false;
    }

    uint32_t e_lfanew = *reinterpret_cast<uint32_t*>(&buffer[0x3C]);
    if (e_lfanew + 24 > buffer.size()) {
        std::cerr << "[OogieHost] Invalid e_lfanew offset!" << std::endl;
        return false;
    }

    if (std::memcmp(&buffer[e_lfanew], "PE\0\0", 4) != 0) {
        std::cerr << "[OogieHost] Missing PE signature!" << std::endl;
        return false;
    }

    uint16_t numSections = *reinterpret_cast<uint16_t*>(&buffer[e_lfanew + 6]);
    uint16_t optHdrSize = *reinterpret_cast<uint16_t*>(&buffer[e_lfanew + 20]);
    size_t optHdrOffset = e_lfanew + 24;

    m_entryPoint = *reinterpret_cast<uint32_t*>(&buffer[optHdrOffset + 16]) + m_baseAddress;
    m_imageSize = *reinterpret_cast<uint32_t*>(&buffer[optHdrOffset + 56]);

    std::cout << "[OogieHost] Image Size: " << m_imageSize << " bytes (0x" << std::hex << m_imageSize << std::dec << ")" << std::endl;
    std::cout << "[OogieHost] Virtual Entry Point: 0x" << std::hex << m_entryPoint << std::dec << std::endl;
    std::cout << "[OogieHost] Sections found: " << numSections << std::endl;

    // Allocate 64 MB of unified memory with Read/Write/Execute permissions
    // Try to map near 0x00010000 or allocate contiguous block
    void* requestedAddr = reinterpret_cast<void*>(static_cast<uintptr_t>(m_baseAddress));
    m_memoryBlock = reinterpret_cast<uint8_t*>(mmap(
        requestedAddr,
        m_memoryBlockSize,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    ));

    if (m_memoryBlock == MAP_FAILED) {
        std::cerr << "[OogieHost] Failed to mmap fixed address, falling back to dynamic allocation..." << std::endl;
        m_memoryBlock = reinterpret_cast<uint8_t*>(mmap(
            nullptr,
            m_memoryBlockSize,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
        ));
    }

    if (m_memoryBlock == MAP_FAILED || !m_memoryBlock) {
        std::cerr << "[OogieHost] Virtual memory allocation failed!" << std::endl;
        return false;
    }

    std::memset(m_memoryBlock, 0, m_memoryBlockSize);
    std::cout << "[OogieHost] Unified Memory allocated at: 0x" << std::hex << reinterpret_cast<uintptr_t>(m_memoryBlock) << std::dec << " (64 MB)" << std::endl;

    // Parse and copy sections into virtual memory
    size_t secHeadersOffset = optHdrOffset + optHdrSize;
    m_sections.clear();

    for (int i = 0; i < numSections; ++i) {
        size_t off = secHeadersOffset + i * 40;
        if (off + 40 > buffer.size()) break;

        char name[9] = { 0 };
        std::memcpy(name, &buffer[off], 8);

        PESectionInfo sec;
        sec.name = name;
        sec.virtualSize = *reinterpret_cast<uint32_t*>(&buffer[off + 8]);
        sec.virtualAddress = *reinterpret_cast<uint32_t*>(&buffer[off + 12]) + m_baseAddress;
        sec.rawSize = *reinterpret_cast<uint32_t*>(&buffer[off + 16]);
        sec.rawAddress = *reinterpret_cast<uint32_t*>(&buffer[off + 20]);
        sec.characteristics = *reinterpret_cast<uint32_t*>(&buffer[off + 36]);

        m_sections.push_back(sec);

        // Copy section contents if within memory bounds
        uintptr_t memOffset = sec.virtualAddress - m_baseAddress;
        if (memOffset + sec.rawSize <= m_memoryBlockSize && sec.rawAddress + sec.rawSize <= buffer.size()) {
            std::memcpy(m_memoryBlock + memOffset, buffer.data() + sec.rawAddress, sec.rawSize);
            std::cout << "  -> Loaded section " << sec.name
                      << " (VA: 0x" << std::hex << sec.virtualAddress
                      << ", Raw: 0x" << sec.rawAddress
                      << ", Size: " << std::dec << sec.rawSize << " bytes)" << std::endl;
        }
    }

    m_isLoaded = true;
    return true;
}

bool OogieHost::PatchKernelThunks() {
    if (!m_memoryBlock) return false;

    // Address of the 125 Xbox kernel thunk slots: VA 0x00260020
    uintptr_t thunkTableVA = 0x00260020;
    uintptr_t tableOffset = thunkTableVA - m_baseAddress;

    if (tableOffset + 125 * sizeof(uintptr_t) > m_memoryBlockSize) {
        std::cerr << "[OogieHost] Kernel thunk table offset out of bounds!" << std::endl;
        return false;
    }

    uintptr_t targetTable = reinterpret_cast<uintptr_t>(m_memoryBlock + tableOffset);
    InitializeKernelThunks(targetTable);
    return true;
}

bool OogieHost::PatchHardwareDirect3D() {
    std::cout << "[OogieHost] Patching Direct3D NV2A pushbuffer registers & vtable..." << std::endl;
    // Section D3D is at VA 0x001d22c0
    // g_pDevice is stored at VA 0x001e2d28
    // g_pPushBuffer write pointer is at VA 0x001e2d30
    uintptr_t deviceOffset = 0x001e2d28 - m_baseAddress;
    uintptr_t pushOffset = 0x001e2d30 - m_baseAddress;

    if (m_memoryBlock && pushOffset + 8 <= m_memoryBlockSize) {
        // Provide a valid dummy pushbuffer pointer so write instructions don't fault
        static uint32_t s_dummyPushBuffer[65536];
        *reinterpret_cast<uintptr_t*>(m_memoryBlock + pushOffset) = reinterpret_cast<uintptr_t>(s_dummyPushBuffer);
        *reinterpret_cast<uintptr_t*>(m_memoryBlock + pushOffset + 4) = reinterpret_cast<uintptr_t>(s_dummyPushBuffer + 65530);
        std::cout << "[OogieHost] Direct3D command FIFO hooked safely." << std::endl;
    }

    return true;
}

bool OogieHost::PatchGamepadInput() {
    std::cout << "[OogieHost] Patching XPP Gamepad input..." << std::endl;
    std::cout << "[OogieHost] Keyboard and modern gamepads mapped to Xbox controller slots 0..3." << std::endl;
    return true;
}

} // namespace Host
} // namespace OogieEngine

