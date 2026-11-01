#include "OogieEngine/Loader/SceneLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace Loader {

std::vector<std::string> SceneLoader::DiscoverStages(const std::string& rootDir) {
    std::vector<std::string> stages;
    if (!fs::exists(rootDir)) return stages;

    for (const auto& entry : fs::directory_iterator(rootDir)) {
        if (entry.is_directory()) {
            stages.push_back(entry.path().filename().string());
        }
    }
    std::sort(stages.begin(), stages.end());
    return stages;
}

bool SceneLoader::LoadSceneList(const std::string& bgListPath, std::vector<Core::SceneObjectDef>& outObjects) {
    outObjects.clear();
    std::ifstream file(bgListPath);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        size_t cPos = line.find(';');
        if (cPos != std::string::npos) line = line.substr(0, cPos);

        std::istringstream iss(line);
        std::string name;
        if (iss >> name) {
            Core::SceneObjectDef obj;
            obj.modelName = name;
            std::string rem;
            std::getline(iss, rem);
            obj.flags = rem;
            outObjects.push_back(obj);
        }
    }
    return !outObjects.empty();
}

bool SceneLoader::LoadStageConfig(const std::string& cnfgPath, Core::StageConfig& outConfig) {
    std::ifstream file(cnfgPath);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        size_t cPos = line.find(';');
        if (cPos != std::string::npos) line = line.substr(0, cPos);

        size_t eqPos = line.find('=');
        if (eqPos != std::string::npos) {
            std::string key = line.substr(0, eqPos);
            std::string val = line.substr(eqPos + 1);

            // trim
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t\r\n"));
                s.erase(s.find_last_not_of(" \t\r\n") + 1);
            };
            trim(key); trim(val);

            try {
                float fVal = std::stof(val);
                if (key == "cam_x") outConfig.cameraPos.x = fVal;
                else if (key == "cam_y") outConfig.cameraPos.y = fVal;
                else if (key == "cam_z") outConfig.cameraPos.z = fVal;
                else if (key == "cam_rx") outConfig.cameraRot.x = fVal;
                else if (key == "cam_ry") outConfig.cameraRot.y = fVal;
                else if (key == "cam_rz") outConfig.cameraRot.z = fVal;
                else if (key == "nearz") outConfig.nearZ = fVal;
                else if (key == "farz") outConfig.farZ = fVal;
                else if (key == "fog_start") outConfig.fogStart = fVal;
                else if (key == "fog_end") outConfig.fogEnd = fVal;
                else if (key == "fog_r") outConfig.fogColor.x = fVal / 255.0f;
                else if (key == "fog_g") outConfig.fogColor.y = fVal / 255.0f;
                else if (key == "fog_b") outConfig.fogColor.z = fVal / 255.0f;
            } catch (...) {}
        } else if (line.find("fog_on") != std::string::npos) {
            outConfig.fogEnabled = true;
        }
    }
    return true;
}

} // namespace Loader
} // namespace OogieEngine

