#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Rendering/GPUTexture.h  —  GPU Texture Cache & Manager
// ─────────────────────────────────────────────────────────────────────────────

#include "raylib.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace OogieEngine {
namespace Rendering {

class GPUTextureCache {
public:
    static GPUTextureCache& Instance();

    void Initialize();
    void Shutdown();
    void Clear();

    Texture2D GetTexture(const std::string& name, const std::string& textureDir = "");
    Texture2D GetDefaultTexture() const { return defaultTexture; }

    const std::unordered_map<std::string, Texture2D>& GetCache() const { return cache; }
    size_t GetLoadedCount() const { return cache.size(); }

private:
    GPUTextureCache() = default;
    ~GPUTextureCache();

    GPUTextureCache(const GPUTextureCache&) = delete;
    GPUTextureCache& operator=(const GPUTextureCache&) = delete;

    Texture2D defaultTexture = { 0 };
    std::unordered_map<std::string, Texture2D> cache;
    bool initialized = false;
};

} // namespace Rendering
} // namespace OogieEngine
