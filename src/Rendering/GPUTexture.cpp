#include "OogieEngine/Rendering/GPUTexture.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace Rendering {

GPUTextureCache& GPUTextureCache::Instance() {
    static GPUTextureCache instance;
    return instance;
}

GPUTextureCache::~GPUTextureCache() {
    Shutdown();
}

void GPUTextureCache::Initialize() {
    if (initialized) return;

    Image check = GenImageChecked(32, 32, 16, 16, MAGENTA, DARKGRAY);
    defaultTexture = LoadTextureFromImage(check);
    UnloadImage(check);

    initialized = true;
}

void GPUTextureCache::Shutdown() {
    Clear();
    if (defaultTexture.id > 0) {
        UnloadTexture(defaultTexture);
        defaultTexture = { 0 };
    }
    initialized = false;
}

void GPUTextureCache::Clear() {
    for (auto& pair : cache) {
        if (pair.second.id > 0 && pair.second.id != defaultTexture.id) {
            UnloadTexture(pair.second);
        }
    }
    cache.clear();
}

Texture2D GPUTextureCache::GetTexture(const std::string& name, const std::string& textureDir) {
    if (!initialized) {
        Initialize();
    }

    if (name.empty()) {
        return defaultTexture;
    }

    auto it = cache.find(name);
    if (it != cache.end()) {
        return it->second;
    }

    std::vector<std::string> searchPaths;
    if (!textureDir.empty()) {
        searchPaths.push_back(textureDir + "/" + name + ".dds");
        searchPaths.push_back(textureDir + "/" + name + ".pig");
        searchPaths.push_back(textureDir + "/" + name);
    }
    searchPaths.push_back("rom/structured_data/stages/0000/textures/" + name + ".dds");
    searchPaths.push_back("rom/structured_data/stages/0000/textures/" + name + ".pig");
    searchPaths.push_back("structured_data/stages/0000/textures/" + name + ".dds");

    for (const auto& path : searchPaths) {
        if (fs::exists(path)) {
            Image img = LoadImage(path.c_str());
            if (img.data != nullptr) {
                Texture2D tex = LoadTextureFromImage(img);
                UnloadImage(img);
                SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
                cache[name] = tex;
                return tex;
            }
        }
    }

    // Cache default fallback to avoid repeated disk misses
    cache[name] = defaultTexture;
    return defaultTexture;
}

} // namespace Rendering
} // namespace OogieEngine
