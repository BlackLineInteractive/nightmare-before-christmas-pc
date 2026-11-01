#pragma once

#include "PodModel.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

struct SceneModelInstance {
    std::string name;
    std::shared_ptr<PodModel> model;
    bool visible = true;
};

class StageScene {
public:
    StageScene();
    ~StageScene();

    bool LoadStage(const std::string& stageDir);
    void Unload();

    void Draw(bool wireframe, bool useTextures, bool useColors);

    BoundingBox GetBoundingBox() const { return sceneBBox; }
    Vector3 GetCenter() const;

    const std::string& GetCurrentStageId() const { return currentStageId; }
    const std::vector<SceneModelInstance>& GetModelInstances() const { return models; }
    std::vector<SceneModelInstance>& GetModelInstances() { return models; }

    int GetTotalTriangles() const;
    int GetTotalVertices() const;

    // Available stages discovered on disk
    static std::vector<std::string> DiscoverStages(const std::string& rootStagesDir = "structured_data/stages");

private:
    std::string currentStageId;
    std::string currentStageDir;
    std::vector<SceneModelInstance> models;
    BoundingBox sceneBBox;

    void ParseBgList(const std::string& bgListPath, std::vector<std::string>& outModelNames);
    void ComputeSceneBounds();
};

