#pragma once

#include "raylib.h"
#include <string>
#include <unordered_map>
#include <vector>

class TextureManager {
public:
    static TextureManager& Instance() {
        static TextureManager instance;
        return instance;
    }

    Texture2D GetTexture(const std::string& texName, const std::string& textureDir = "");
    void Clear();
    Texture2D GetDefaultTexture() const { return defaultTexture; }

    const std::unordered_map<std::string, Texture2D>& GetAllLoaded() const {
        return loadedTextures;
    }

private:
    TextureManager();
    ~TextureManager();

    std::unordered_map<std::string, Texture2D> loadedTextures;
    Texture2D defaultTexture;
};

