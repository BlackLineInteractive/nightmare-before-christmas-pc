#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Core/VFS.h  —  Virtual File System for Capcom RD3
// Transparently handles Xbox "D:\", Capcom "host0:", and relative package paths.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace OogieEngine {
namespace Core {

class VFS {
public:
    static VFS& Instance();

    void Initialize(const std::string& rootDataPath);
    void SetSearchPath(const std::string& prefix, const std::string& realPath);

    // Resolves Xbox/Capcom paths like "D:\stm_data\0000.lz" or "host0:data/world/0000/scene/bg_list.txt"
    std::string ResolvePath(const std::string& virtualPath) const;

    bool FileExists(const std::string& virtualPath) const;
    std::vector<uint8_t> ReadFile(const std::string& virtualPath) const;
    std::string ReadText(const std::string& virtualPath) const;

private:
    VFS() = default;
    std::string rootPath = "rom";
    std::unordered_map<std::string, std::string> mountPoints;

    std::string NormalizePath(const std::string& path) const;
};

} // namespace Core
} // namespace OogieEngine
