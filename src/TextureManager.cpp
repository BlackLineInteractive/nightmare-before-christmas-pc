#include "TextureManager.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

TextureManager::TextureManager() {
    // 2x2 magenta/dark checkerboard for missing textures
    Image check = GenImageChecked(32, 32, 16, 16, MAGENTA, DARKGRAY);
    defaultTexture = LoadTextureFromImage(check);
    UnloadImage(check);
}

TextureManager::~TextureManager() {
    Clear();
    if (defaultTexture.id > 0) {
        UnloadTexture(defaultTexture);
    }
}

void TextureManager::Clear() {
    for (auto& pair : loadedTextures) {
        if (pair.second.id > 0) {
            UnloadTexture(pair.second);
        }
    }
    loadedTextures.clear();
}

Texture2D TextureManager::GetTexture(const std::string& texName, const std::string& textureDir) {
    if (texName.empty()) {
        return defaultTexture;
    }

    auto it = loadedTextures.find(texName);
    if (it != loadedTextures.end()) {
        return it->second;
    }

    std::vector<std::string> searchPaths;
    if (!textureDir.empty()) {
        searchPaths.push_back(textureDir + "/" + texName + ".dds");
        searchPaths.push_back(textureDir + "/" + texName + ".pig");
        searchPaths.push_back(textureDir + "/" + texName);
    }
    // Also common fallback paths
    searchPaths.push_back("structured_data/stages/0000/textures/" + texName + ".dds");
    searchPaths.push_back("structured_data/stages/0000/textures/" + texName + ".pig");

    for (const auto& path : searchPaths) {
        if (fs::exists(path)) {
            Image img = LoadImage(path.c_str());
            if (img.data != nullptr) {
                Texture2D tex = LoadTextureFromImage(img);
                UnloadImage(img);
                SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
                loadedTextures[texName] = tex;
                return tex;
            }
        }
    }

    // Cache default texture so we don't spam disk checks
    loadedTextures[texName] = defaultTexture;
    return defaultTexture;
}

