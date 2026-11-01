#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Loader/SceneLoader.h  —  bg_list.txt & cnfg.txt Parser
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Core/Types.h"
#include <string>
#include <vector>

namespace OogieEngine {
namespace Loader {

class SceneLoader {
public:
    static bool LoadSceneList(const std::string& bgListPath, std::vector<Core::SceneObjectDef>& outObjects);
    static bool LoadStageConfig(const std::string& cnfgPath, Core::StageConfig& outConfig);
    static std::vector<std::string> DiscoverStages(const std::string& rootDir);
};

} // namespace Loader
} // namespace OogieEngine
