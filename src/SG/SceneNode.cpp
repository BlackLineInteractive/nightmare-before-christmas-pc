#include "OogieEngine/SG/SceneNode.h"

namespace OogieEngine {
namespace SG {

SceneNode::SceneNode(const std::string& nodeName)
    : name(nodeName)
{
}

void SceneNode::AddChild(std::shared_ptr<SceneNode> child) {
    if (child) {
        child->parent = shared_from_this();
        children.push_back(child);
    }
}

void SceneNode::RemoveChild(std::shared_ptr<SceneNode> child) {
    auto it = std::find(children.begin(), children.end(), child);
    if (it != children.end()) {
        (*it)->parent.reset();
        children.erase(it);
    }
}

void SceneNode::Update(const Matrix& parentWorld) {
    worldTransform = MatrixMultiply(localTransform, parentWorld);
    for (auto& child : children) {
        child->Update(worldTransform);
    }
}

void SceneNode::Draw(Rendering::RenderMode mode) {
    if (!visible) return;

    if (model) {
        model->Draw(mode, worldTransform);
    }

    for (auto& child : children) {
        child->Draw(mode);
    }
}

Core::BoundingBox SceneNode::GetWorldBoundingBox() const {
    Core::BoundingBox result;
    bool hasBounds = false;

    if (model) {
        const auto& b = model->GetBoundingBox();
        Vector3 corners[8] = {
            { b.min.x, b.min.y, b.min.z },
            { b.max.x, b.min.y, b.min.z },
            { b.min.x, b.max.y, b.min.z },
            { b.max.x, b.max.y, b.min.z },
            { b.min.x, b.min.y, b.max.z },
            { b.max.x, b.min.y, b.max.z },
            { b.min.x, b.max.y, b.max.z },
            { b.max.x, b.max.y, b.max.z }
        };

        for (int i = 0; i < 8; i++) {
            Vector3 transformed = Vector3Transform(corners[i], worldTransform);
            if (!hasBounds) {
                result.min = { transformed.x, transformed.y, transformed.z };
                result.max = { transformed.x, transformed.y, transformed.z };
                hasBounds = true;
            } else {
                result.min.x = std::min(result.min.x, transformed.x);
                result.min.y = std::min(result.min.y, transformed.y);
                result.min.z = std::min(result.min.z, transformed.z);

                result.max.x = std::max(result.max.x, transformed.x);
                result.max.y = std::max(result.max.y, transformed.y);
                result.max.z = std::max(result.max.z, transformed.z);
            }
        }
    }

    for (const auto& child : children) {
        if (!child->IsVisible()) continue;
        Core::BoundingBox childBox = child->GetWorldBoundingBox();
        if (childBox.min != childBox.max) {
            if (!hasBounds) {
                result = childBox;
                hasBounds = true;
            } else {
                result.min.x = std::min(result.min.x, childBox.min.x);
                result.min.y = std::min(result.min.y, childBox.min.y);
                result.min.z = std::min(result.min.z, childBox.min.z);

                result.max.x = std::max(result.max.x, childBox.max.x);
                result.max.y = std::max(result.max.y, childBox.max.y);
                result.max.z = std::max(result.max.z, childBox.max.z);
            }
        }
    }

    return result;
}

} // namespace SG
} // namespace OogieEngine
