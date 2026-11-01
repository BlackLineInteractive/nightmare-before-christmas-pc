#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/UI/Panels/DialoguePanel.h  —  Multi-language BMS Dialogue Reader
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

namespace OogieEngine {
namespace UI {

class DialoguePanel {
public:
    DialoguePanel() = default;

    void Draw(bool* pOpen, const std::string& currentStagePath);

private:
    std::string currentLoadedPath;
    std::vector<std::string> dialogueLines;
    int selectedLangIndex = 0; // 0: English (US), 1: British, 2: French, 3: German, 4: Italian, 5: Spanish
    int selectedScriptIndex = 0;
    char textSearchFilter[64] = "";

    void ReloadDialogues(const std::string& stagePath);
};

} // namespace UI
} // namespace OogieEngine

