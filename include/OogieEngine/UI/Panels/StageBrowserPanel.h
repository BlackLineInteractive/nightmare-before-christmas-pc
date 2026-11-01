#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/UI/Panels/StageBrowserPanel.h  —  Stage & World Browser
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>
#include <functional>

namespace OogieEngine {
namespace UI {

struct StageEntry {
    std::string id;
    std::string path;
    std::string worldName;
    int modelCount = 0;
    int textureCount = 0;
    bool hasScene = false;
    bool hasMessages = false;
};

class StageBrowserPanel {
public:
    StageBrowserPanel();

    void ScanStages(const std::string& baseDir);
    void Draw(bool* pOpen, const std::function<void(const std::string&)>& onStageSelected);

    const std::string& GetSelectedStageId() const { return selectedStageId; }

private:
    std::vector<StageEntry> stages;
    std::string selectedStageId;
    char searchFilter[64] = "";

    std::string GetWorldNameForStage(const std::string& stageId) const;
};

} // namespace UI
} // namespace OogieEngine

