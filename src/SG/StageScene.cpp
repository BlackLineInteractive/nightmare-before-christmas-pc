#include "OogieEngine/SG/StageScene.h"
#include "OogieEngine/Loader/PodLoader.h"
#include "OogieEngine/Loader/SceneLoader.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace SG {

StageScene::StageScene() {
    rootNode = std::make_shared<SceneNode>("StageRoot");
}

StageScene::~StageScene() {
    Unload();
}

void StageScene::Unload() {
    modelNodes.clear();
    modelCache.clear();
    rootNode = std::make_shared<SceneNode>("StageRoot");
    totalVertices = 0;
    totalTriangles = 0;
    isLoaded = false;
    stageId.clear();
    stagePath.clear();
    bbox = Core::BoundingBox{};
}

bool StageScene::LoadStage(const std::string& stageDir) {
    Unload();

    if (!fs::exists(stageDir)) {
        std::cerr << "[StageScene] Stage path does not exist: " << stageDir << std::endl;
        return false;
    }

    stagePath = stageDir;
    stageId = fs::path(stageDir).filename().string();

    std::string sceneDir = stageDir + "/scene";
    std::string modelsDir = stageDir + "/models";
    std::string texturesDir = stageDir + "/textures";

    // 1. Load config if present
    std::string cnfgPath = sceneDir + "/cnfg.txt";
    if (fs::exists(cnfgPath)) {
        Loader::SceneLoader::LoadStageConfig(cnfgPath, config);
    }

    // 2. Discover models from bg_list.txt or directory
    std::vector<std::string> modelFileNames;
    std::string bgListPath = sceneDir + "/bg_list.txt";

    if (fs::exists(bgListPath)) {
        std::vector<Core::SceneObjectDef> sceneObjects;
        if (Loader::SceneLoader::LoadSceneList(bgListPath, sceneObjects)) {
            for (const auto& obj : sceneObjects) {
                modelFileNames.push_back(obj.modelName);
            }
        }
    }

    // Fallback: load all .pod files found in models/
    if (modelFileNames.empty() && fs::exists(modelsDir)) {
        for (const auto& entry : fs::directory_iterator(modelsDir)) {
            if (entry.path().extension() == ".pod") {
                modelFileNames.push_back(entry.path().stem().string());
            }
        }
        std::sort(modelFileNames.begin(), modelFileNames.end());
    }

    // 3. Load and upload each model
    for (const auto& name : modelFileNames) {
        std::string podPath = modelsDir + "/" + name + ".pod";
        if (!fs::exists(podPath)) {
            // Try with _p2 suffix
            podPath = modelsDir + "/" + name + "_p2.pod";
            if (!fs::exists(podPath)) continue;
        }

        std::string txiPath = modelsDir + "/" + name + ".txi";
        if (!fs::exists(txiPath)) {
            txiPath = modelsDir + "/" + name + "_p2.txi";
        }

        std::shared_ptr<Rendering::GPUModel> gpuModel;
        auto it = modelCache.find(name);
        if (it != modelCache.end()) {
            gpuModel = it->second;
        } else {
            Core::ModelData cpuData;
            if (Loader::PodLoader::Load(podPath, cpuData, txiPath)) {
                gpuModel = std::make_shared<Rendering::GPUModel>();
                if (gpuModel->Upload(cpuData, texturesDir)) {
                    modelCache[name] = gpuModel;
                } else {
                    gpuModel.reset();
                }
            }
        }

        if (gpuModel) {
            auto node = std::make_shared<SceneNode>(name);
            node->SetModel(gpuModel);
            rootNode->AddChild(node);
            modelNodes.push_back(node);

            totalVertices += gpuModel->GetVertexCount();
            totalTriangles += gpuModel->GetTriangleCount();
        }
    }

    rootNode->Update();
    bbox = rootNode->GetWorldBoundingBox();
    isLoaded = !modelNodes.empty();

    std::cout << "[StageScene] Successfully loaded stage: " << stageId
              << " (" << modelNodes.size() << " models, "
              << totalVertices << " verts, "
              << totalTriangles << " tris)" << std::endl;

    return isLoaded;
}

void StageScene::Update(float dt) {
    (void)dt;
    if (rootNode) {
        rootNode->Update();
    }
}

void StageScene::Draw(Rendering::RenderMode mode) {
    if (rootNode) {
        rootNode->Draw(mode);
    }
}

} // namespace SG
} // namespace OogieEngine
