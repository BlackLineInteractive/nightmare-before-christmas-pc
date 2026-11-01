#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/SG/StageScene.h  —  Complete Stage / Level representation
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Core/Types.h"
#include "OogieEngine/SG/SceneNode.h"
#include "OogieEngine/Rendering/GPUMesh.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace OogieEngine {
namespace SG {

class StageScene {
public:
    StageScene();
    ~StageScene();

    bool LoadStage(const std::string& stageDir);
    void Unload();

    void Update(float dt);
    void Draw(Rendering::RenderMode mode);

    const std::string& GetStageId() const { return stageId; }
    const std::string& GetStagePath() const { return stagePath; }
    const Core::StageConfig& GetConfig() const { return config; }
    const Core::BoundingBox& GetBoundingBox() const { return bbox; }

    std::shared_ptr<SceneNode> GetRootNode() const { return rootNode; }
    const std::vector<std::shared_ptr<SceneNode>>& GetModelNodes() const { return modelNodes; }

    int GetTotalModels() const { return static_cast<int>(modelNodes.size()); }
    int GetTotalVertices() const { return totalVertices; }
    int GetTotalTriangles() const { return totalTriangles; }

    bool IsLoaded() const { return isLoaded; }

private:
    std::string stageId;
    std::string stagePath;
    Core::StageConfig config;
    Core::BoundingBox bbox;

    std::shared_ptr<SceneNode> rootNode;
    std::vector<std::shared_ptr<SceneNode>> modelNodes;
    std::unordered_map<std::string, std::shared_ptr<Rendering::GPUModel>> modelCache;

    int totalVertices = 0;
    int totalTriangles = 0;
    bool isLoaded = false;
};

} // namespace SG
} // namespace OogieEngine

