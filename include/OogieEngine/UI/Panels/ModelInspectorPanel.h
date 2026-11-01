#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/UI/Panels/ModelInspectorPanel.h  —  Scene & Model Hierarchy
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/SG/StageScene.h"
#include <functional>

namespace OogieEngine {
namespace UI {

class ModelInspectorPanel {
public:
    ModelInspectorPanel() = default;

    void Draw(bool* pOpen, SG::StageScene& scene,
              const std::function<void(const Core::BoundingBox&)>& onFocusRequested);

private:
    char searchFilter[64] = "";
    int selectedIndex = -1;
};

} // namespace UI
} // namespace OogieEngine
