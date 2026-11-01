#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Rendering/GPUMesh.h  —  GPU-side Model & Mesh representation
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Core/Types.h"
#include "raylib.h"
#include "raymath.h"
#include <vector>
#include <string>
#include <memory>

namespace OogieEngine {
namespace Rendering {

enum class RenderMode {
    Textured = 0,
    VertexColor,
    Wireframe,
    FlatShaded,
    Normals
};

struct GPUSubMesh {
    std::vector<Core::Vertex> vertices;
    std::string textureName;
    Texture2D texture = { 0 };
};

struct GPUObject {
    Matrix transform;
    std::vector<GPUSubMesh> submeshes;
};

class GPUModel {
public:
    GPUModel() = default;
    ~GPUModel();

    bool Upload(const Core::ModelData& cpuData, const std::string& textureDir = "");
    void Unload();

    void Draw(RenderMode mode, Matrix worldTransform = MatrixIdentity()) const;

    const Core::BoundingBox& GetBoundingBox() const { return bbox; }
    const std::string& GetName() const { return name; }
    const std::vector<std::string>& GetTextureList() const { return textures; }

    int GetTriangleCount() const { return triangleCount; }
    int GetVertexCount() const { return vertexCount; }

private:
    std::string name;
    std::vector<GPUObject> objects;
    std::vector<std::string> textures;
    Core::BoundingBox bbox;
    int triangleCount = 0;
    int vertexCount = 0;
};

} // namespace Rendering
} // namespace OogieEngine

