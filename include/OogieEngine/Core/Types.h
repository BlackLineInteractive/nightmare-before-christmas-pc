#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Core/Types.h  —  Pure CPU data types, NO GL, NO Raylib, NO ImGui
//
// Matches Climax Game Engine Toolkit design architecture.
// Every translation unit in core and loader includes this header.
// GPU handles live strictly in Rendering/GPUMesh.h and Rendering/GPUTexture.h.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace OogieEngine {
namespace Core {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    bool operator==(const Vec3& o) const {
        return x == o.x && y == o.y && z == o.z;
    }
    bool operator!=(const Vec3& o) const {
        return !(*this == o);
    }
};

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

struct Mat4 {
    std::array<float, 16> m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
};

struct BoundingBox {
    Vec3 min = { 0.0f, 0.0f, 0.0f };
    Vec3 max = { 0.0f, 0.0f, 0.0f };

    Vec3 Center() const {
        return {
            (min.x + max.x) * 0.5f,
            (min.y + max.y) * 0.5f,
            (min.z + max.z) * 0.5f
        };
    }

    float MaxDimension() const {
        return std::max({ max.x - min.x, max.y - min.y, max.z - min.z });
    }
};

struct Vertex {
    Vec3 pos;
    Vec3 normal = { 0.0f, 1.0f, 0.0f };
    Vec2 uv;
    Vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
};

struct SubMeshChunk {
    std::vector<Vertex> vertices;
    std::string textureName;
    int textureIndex = -1;
    BoundingBox bbox;
};

struct ModelObject {
    Mat4 transform;
    std::vector<SubMeshChunk> submeshes;
    BoundingBox bbox;
};

struct ModelData {
    std::string name;
    std::vector<ModelObject> objects;
    std::vector<std::string> textureList;
    BoundingBox bbox;

    int GetTotalVertices() const {
        int count = 0;
        for (const auto& o : objects) {
            for (const auto& s : o.submeshes) {
                count += static_cast<int>(s.vertices.size());
            }
        }
        return count;
    }

    int GetTotalTriangles() const {
        return GetTotalVertices() / 3;
    }
};

struct SceneObjectDef {
    std::string modelName;
    std::string flags;
    float scrollU = 0.0f;
    float scrollV = 0.0f;
};

struct StageConfig {
    Vec3 cameraPos = { 0.0f, 0.0f, 0.0f };
    Vec3 cameraRot = { 0.0f, 0.0f, 0.0f };
    float nearZ = 0.5f;
    float farZ = 10000.0f;
    bool fogEnabled = false;
    float fogStart = 50.0f;
    float fogEnd = 400.0f;
    Vec3 fogColor = { 0.25f, 0.25f, 0.28f };
    Vec3 lightDir = { -0.5f, -0.7f, -0.3f };
    Vec3 lightColor = { 1.0f, 1.0f, 1.0f };
};

struct DialogueMessage {
    int id = 0;
    std::string text;
};

} // namespace Core
} // namespace OogieEngine

