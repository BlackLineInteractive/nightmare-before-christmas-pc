#include "OogieEngine/Core/VFS.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace Core {

VFS& VFS::Instance() {
    static VFS instance;
    return instance;
}

void VFS::Initialize(const std::string& rootDataPath) {
    rootPath = rootDataPath;
    mountPoints.clear();

    // Default mount points
    mountPoints["d:"] = rootDataPath;
    mountPoints["d:/"] = rootDataPath;
    mountPoints["host0:"] = rootDataPath;
    mountPoints["host0:data/"] = rootDataPath + "/structured_data/";
    mountPoints["host0:data"] = rootDataPath + "/structured_data";
}

void VFS::SetSearchPath(const std::string& prefix, const std::string& realPath) {
    mountPoints[prefix] = realPath;
}

std::string VFS::NormalizePath(const std::string& path) const {
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');

    // Remove redundant leading slashes
    while (norm.rfind("//", 0) == 0) {
        norm.erase(0, 1);
    }
    return norm;
}

std::string VFS::ResolvePath(const std::string& virtualPath) const {
    std::string p = NormalizePath(virtualPath);

    // 1. Check exact mounts
    for (const auto& [prefix, realDir] : mountPoints) {
        if (p.rfind(prefix, 0) == 0) {
            std::string sub = p.substr(prefix.length());
            if (!sub.empty() && sub.front() == '/') sub.erase(0, 1);
            std::string resolved = realDir + "/" + sub;
            if (fs::exists(resolved)) return resolved;
        }
    }

    // 2. Strip drive letters (e.g. "D:/stm_data/..." -> "stm_data/...")
    if (p.size() >= 2 && (p[1] == ':' || (p[0] == 'D' || p[0] == 'd'))) {
        size_t colon = p.find(':');
        if (colon != std::string::npos) {
            p = p.substr(colon + 1);
            if (!p.empty() && p.front() == '/') p.erase(0, 1);
        }
    }

    // 3. Search common fallback roots
    const std::vector<std::string> candidates = {
        p,
        rootPath + "/" + p,
        rootPath + "/structured_data/" + p,
        rootPath + "/extracted/" + p,
        rootPath + "/structured_data/stages/" + p,
        "rom/structured_data/" + p,
        "rom/extracted/" + p,
        "rom/structured_data/stages/" + p,
        "rom/" + p
    };

    for (const auto& c : candidates) {
        if (fs::exists(c)) {
            return c;
        }
    }

    return p;
}

bool VFS::FileExists(const std::string& virtualPath) const {
    std::string resolved = ResolvePath(virtualPath);
    return fs::exists(resolved);
}

std::vector<uint8_t> VFS::ReadFile(const std::string& virtualPath) const {
    std::string resolved = ResolvePath(virtualPath);
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[VFS] Failed to open file: " << virtualPath << " (resolved to " << resolved << ")\n";
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }
    return {};
}

std::string VFS::ReadText(const std::string& virtualPath) const {
    std::vector<uint8_t> data = ReadFile(virtualPath);
    if (data.empty()) return "";
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace Core
} // namespace OogieEngine
