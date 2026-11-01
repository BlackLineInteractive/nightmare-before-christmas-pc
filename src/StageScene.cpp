#include "StageScene.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

StageScene::StageScene() {
    sceneBBox.min = {0, 0, 0};
    sceneBBox.max = {0, 0, 0};
}

StageScene::~StageScene() {
    Unload();
}

void StageScene::Unload() {
    models.clear();
    currentStageId.clear();
    sceneBBox.min = {0, 0, 0};
    sceneBBox.max = {0, 0, 0};
}

std::vector<std::string> StageScene::DiscoverStages(const std::string& rootStagesDir) {
    std::vector<std::string> stages;
    if (!fs::exists(rootStagesDir)) return stages;

    for (const auto& entry : fs::directory_iterator(rootStagesDir)) {
        if (entry.is_directory()) {
            stages.push_back(entry.path().filename().string());
        }
    }
    std::sort(stages.begin(), stages.end());
    return stages;
}

void StageScene::ParseBgList(const std::string& bgListPath, std::vector<std::string>& outModelNames) {
    std::ifstream file(bgListPath);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        // Strip comments and leading/trailing whitespace
        size_t commentPos = line.find(';');
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        std::istringstream iss(line);
        std::string modelName;
        if (iss >> modelName) {
            outModelNames.push_back(modelName);
        }
    }
}

Vector3 StageScene::GetCenter() const {
    return Vector3Scale(Vector3Add(sceneBBox.min, sceneBBox.max), 0.5f);
}

void StageScene::ComputeSceneBounds() {
    if (models.empty()) {
        sceneBBox = {{0,0,0}, {0,0,0}};
        return;
    }

    bool first = true;
    for (const auto& inst : models) {
        if (!inst.visible || !inst.model) continue;
        BoundingBox b = inst.model->GetBoundingBox();
        if (first) {
            sceneBBox = b;
            first = false;
        } else {
            sceneBBox.min.x = std::min(sceneBBox.min.x, b.min.x);
            sceneBBox.min.y = std::min(sceneBBox.min.y, b.min.y);
            sceneBBox.min.z = std::min(sceneBBox.min.z, b.min.z);

            sceneBBox.max.x = std::max(sceneBBox.max.x, b.max.x);
            sceneBBox.max.y = std::max(sceneBBox.max.y, b.max.y);
            sceneBBox.max.z = std::max(sceneBBox.max.z, b.max.z);
        }
    }
}

bool StageScene::LoadStage(const std::string& stageDir) {
    Unload();
    currentStageDir = stageDir;
    currentStageId = fs::path(stageDir).filename().string();

    std::string bgListPath = stageDir + "/scene/bg_list.txt";
    std::string modelsDir = stageDir + "/models";
    std::string texturesDir = stageDir + "/textures";

    std::vector<std::string> modelNames;
    if (fs::exists(bgListPath)) {
        ParseBgList(bgListPath, modelNames);
    }

    // If bg_list is empty or not found, load all .pod files in models/
    if (modelNames.empty() && fs::exists(modelsDir)) {
        for (const auto& entry : fs::directory_iterator(modelsDir)) {
            if (entry.path().extension() == ".pod") {
                modelNames.push_back(entry.path().stem().string());
            }
        }
    }

    for (const auto& mName : modelNames) {
        std::string podPath = modelsDir + "/" + mName + ".pod";
        if (fs::exists(podPath)) {
            auto model = std::make_shared<PodModel>();
            if (model->Load(podPath, texturesDir)) {
                SceneModelInstance inst;
                inst.name = mName;
                inst.model = model;
                inst.visible = true;
                models.push_back(inst);
            }
        }
    }

    ComputeSceneBounds();
    std::cout << "Loaded stage " << currentStageId << " with " << models.size() << " models." << std::endl;
    return !models.empty();
}

void StageScene::Draw(bool wireframe, bool useTextures, bool useColors) {
    for (const auto& inst : models) {
        if (inst.visible && inst.model) {
            inst.model->Draw(wireframe, useTextures, useColors);
        }
    }
}

int StageScene::GetTotalTriangles() const {
    int count = 0;
    for (const auto& inst : models) {
        if (inst.visible && inst.model) {
            count += inst.model->GetTotalTriangles();
        }
    }
    return count;
}

int StageScene::GetTotalVertices() const {
    int count = 0;
    for (const auto& inst : models) {
        if (inst.visible && inst.model) {
            count += inst.model->GetTotalVertices();
        }
    }
    return count;
}

