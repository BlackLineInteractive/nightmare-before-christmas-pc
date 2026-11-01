#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Loader/PodLoader.h  —  Capcom P2OD 3D Model Parser
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Core/Types.h"
#include <string>
#include <vector>

namespace OogieEngine {
namespace Loader {

class PodLoader {
public:
    static bool Load(const std::string& podPath, Core::ModelData& outModel, const std::string& txiPath = "");
    static bool LoadTxi(const std::string& txiPath, std::vector<std::string>& outTextureNames);

private:
    static void ComputeBounds(Core::ModelData& model);
};

} // namespace Loader
} // namespace OogieEngine

