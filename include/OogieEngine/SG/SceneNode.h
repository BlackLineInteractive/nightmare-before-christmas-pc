#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/SG/SceneNode.h  —  Scene Graph Node
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Core/Types.h"
#include "OogieEngine/Rendering/GPUMesh.h"
#include "raylib.h"
#include "raymath.h"
#include <string>
#include <vector>
#include <memory>

namespace OogieEngine {
namespace SG {

class SceneNode : public std::enable_shared_from_this<SceneNode> {
public:
    SceneNode(const std::string& name = "Node");
    virtual ~SceneNode() = default;

    const std::string& GetName() const { return name; }
    void SetName(const std::string& newName) { name = newName; }

    bool IsVisible() const { return visible; }
    void SetVisible(bool v) { visible = v; }

    void SetLocalTransform(const Matrix& m) { localTransform = m; }
    const Matrix& GetLocalTransform() const { return localTransform; }
    const Matrix& GetWorldTransform() const { return worldTransform; }

    void SetModel(std::shared_ptr<Rendering::GPUModel> newModel) { model = newModel; }
    std::shared_ptr<Rendering::GPUModel> GetModel() const { return model; }

    void AddChild(std::shared_ptr<SceneNode> child);
    void RemoveChild(std::shared_ptr<SceneNode> child);
    const std::vector<std::shared_ptr<SceneNode>>& GetChildren() const { return children; }

    enum class BlendMode {
        Opaque = 0,
        AlphaBlend,
        Additive
    };

    BlendMode GetBlendMode() const { return blendMode; }
    void SetBlendMode(BlendMode mode) { blendMode = mode; }

    virtual void Update(const Matrix& parentWorld = MatrixIdentity());
    virtual void Draw(Rendering::RenderMode mode);

    Core::BoundingBox GetWorldBoundingBox() const;

private:
    std::string name;
    bool visible = true;
    BlendMode blendMode = BlendMode::Opaque;
    Matrix localTransform = MatrixIdentity();
    Matrix worldTransform = MatrixIdentity();
    std::shared_ptr<Rendering::GPUModel> model;
    std::weak_ptr<SceneNode> parent;
    std::vector<std::shared_ptr<SceneNode>> children;
};

} // namespace SG
} // namespace OogieEngine

