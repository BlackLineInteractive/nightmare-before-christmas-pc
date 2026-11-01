#include "OogieEngine/UI/Panels/DialoguePanel.h"
#include "imgui.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace UI {

static const char* kLanguages[] = {
    "English (US) [_u]",
    "English (UK) [_b]",
    "French [_f]",
    "German [_g]",
    "Italian [_i]",
    "Spanish [_s]"
};

static const char* kLangSuffixes[] = {
    "_u.txt",
    "_b.txt",
    "_f.txt",
    "_g.txt",
    "_i.txt",
    "_s.txt"
};

void DialoguePanel::ReloadDialogues(const std::string& stagePath) {
    dialogueLines.clear();
    if (stagePath.empty()) return;

    std::string msgDir = stagePath + "/messages";
    if (!fs::exists(msgDir)) return;

    const char* scriptPrefixes[] = { "talk", "event", "quiz" };
    std::string targetFile = msgDir + "/" + scriptPrefixes[selectedScriptIndex] + kLangSuffixes[selectedLangIndex];

    if (!fs::exists(targetFile)) {
        // Fallback without lang suffix if any
        targetFile = msgDir + "/" + scriptPrefixes[selectedScriptIndex] + ".txt";
    }

    if (fs::exists(targetFile)) {
        std::ifstream file(targetFile);
        std::string line;
        std::string currentBlock;

        while (std::getline(file, line)) {
            if (line.find("---") != std::string::npos) {
                if (!currentBlock.empty()) {
                    dialogueLines.push_back(currentBlock);
                    currentBlock.clear();
                }
            } else {
                if (!currentBlock.empty()) currentBlock += "\n";
                currentBlock += line;
            }
        }
        if (!currentBlock.empty()) {
            dialogueLines.push_back(currentBlock);
        }
    }
}

void DialoguePanel::Draw(bool* pOpen, const std::string& currentStagePath) {
    if (!pOpen || !*pOpen) return;

    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Story & Dialogue Reader", pOpen)) {
        if (currentStagePath.empty()) {
            ImGui::TextDisabled("Load a stage to view dialogues.");
            ImGui::End();
            return;
        }

        std::string msgDir = currentStagePath + "/messages";
        if (!fs::exists(msgDir)) {
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1.0f), "No messages folder found in stage.");
            ImGui::End();
            return;
        }

        bool needReload = (currentLoadedPath != currentStagePath);

        const char* scripts[] = { "talk (NPC Dialogues)", "event (Cutscenes)", "quiz (Mayor Questions)" };
        ImGui::TextColored(ImVec4(0.28f, 0.85f, 0.55f, 1.0f), "Capcom RD3 BMS Script Viewer");

        if (ImGui::Combo("Script", &selectedScriptIndex, scripts, IM_ARRAYSIZE(scripts))) {
            needReload = true;
        }

        if (ImGui::Combo("Language", &selectedLangIndex, kLanguages, IM_ARRAYSIZE(kLanguages))) {
            needReload = true;
        }

        if (needReload) {
            currentLoadedPath = currentStagePath;
            ReloadDialogues(currentStagePath);
        }

        ImGui::InputTextWithHint("##MsgFilter", "Search text...", textSearchFilter, sizeof(textSearchFilter));
        ImGui::Separator();

        ImGui::TextDisabled("Messages Found: %zu", dialogueLines.size());

        ImGui::BeginChild("MessagesScroll", ImVec2(0, 0), true);
        for (size_t i = 0; i < dialogueLines.size(); i++) {
            const auto& msg = dialogueLines[i];
            if (textSearchFilter[0] != '\0' && msg.find(textSearchFilter) == std::string::npos) {
                continue;
            }

            ImGui::PushID(static_cast<int>(i));

            ImGui::BeginChild("msg_frame", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
            ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), "Message #%zu:", i + 1);
            ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 25.0f);
            ImGui::TextUnformatted(msg.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();

            ImGui::Spacing();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace UI
} // namespace OogieEngine
