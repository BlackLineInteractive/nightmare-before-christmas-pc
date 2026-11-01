#include "OogieEngine/UI/Panels/StageBrowserPanel.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>
#include <map>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace UI {

StageBrowserPanel::StageBrowserPanel() {
    ScanStages("rom/structured_data/stages");
    if (stages.empty()) {
        ScanStages("structured_data/stages");
    }
}

std::string StageBrowserPanel::GetWorldNameForStage(const std::string& stageId) const {
    if (stageId.rfind("00", 0) == 0) return "Halloween Town (00xx)";
    if (stageId.rfind("01", 0) == 0) return "Hinterlands & Forest (01xx)";
    if (stageId.rfind("02", 0) == 0) return "Dr. Finkelstein's Lab (02xx)";
    if (stageId.rfind("03", 0) == 0) return "Oogie's Casino (03xx)";
    if (stageId.rfind("07", 0) == 0) return "Christmas Town (07xx)";
    if (stageId.rfind("0c", 0) == 0) return "Giant Oogie Final Boss (0cxx)";
    if (stageId.find("trap") != std::string::npos ||
        stageId.find("treat") != std::string::npos ||
        stageId.find("trick") != std::string::npos ||
        stageId.find("zero") != std::string::npos) {
        return "Minigames & Boss Rooms";
    }
    return "Other Stages";
}

void StageBrowserPanel::ScanStages(const std::string& baseDir) {
    stages.clear();
    if (!fs::exists(baseDir)) return;

    for (const auto& entry : fs::directory_iterator(baseDir)) {
        if (!entry.is_directory()) continue;

        StageEntry stage;
        stage.id = entry.path().filename().string();
        stage.path = entry.path().string();
        stage.worldName = GetWorldNameForStage(stage.id);

        std::string modDir = stage.path + "/models";
        if (fs::exists(modDir)) {
            for (const auto& fe : fs::directory_iterator(modDir)) {
                if (fe.path().extension() == ".pod") stage.modelCount++;
            }
        }

        std::string texDir = stage.path + "/textures";
        if (fs::exists(texDir)) {
            for (const auto& fe : fs::directory_iterator(texDir)) {
                if (fe.path().extension() == ".dds" || fe.path().extension() == ".pig") stage.textureCount++;
            }
        }

        stage.hasScene = fs::exists(stage.path + "/scene/bg_list.txt");
        stage.hasMessages = fs::exists(stage.path + "/messages");

        stages.push_back(stage);
    }

    std::sort(stages.begin(), stages.end(), [](const StageEntry& a, const StageEntry& b) {
        return a.id < b.id;
    });
}

void StageBrowserPanel::Draw(bool* pOpen, const std::function<void(const std::string&)>& onStageSelected) {
    if (!pOpen || !*pOpen) return;

    ImGui::SetNextWindowSize(ImVec2(380, 520), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Stage Browser", pOpen)) {
        ImGui::TextColored(ImVec4(0.28f, 0.85f, 0.55f, 1.0f), "Nightmare Before Christmas Stages");
        ImGui::TextDisabled("Extracted RD3 stm_data Packages: %zu", stages.size());
        ImGui::Separator();

        ImGui::InputTextWithHint("##Filter", "Search stages...", searchFilter, sizeof(searchFilter));
        ImGui::SameLine();
        if (ImGui::Button("Rescan")) {
            ScanStages("rom/structured_data/stages");
            if (stages.empty()) ScanStages("structured_data/stages");
        }

        ImGui::Separator();

        // Group stages by world
        std::map<std::string, std::vector<const StageEntry*>> worldGroups;
        for (const auto& st : stages) {
            if (searchFilter[0] != '\0') {
                if (st.id.find(searchFilter) == std::string::npos &&
                    st.worldName.find(searchFilter) == std::string::npos) {
                    continue;
                }
            }
            worldGroups[st.worldName].push_back(&st);
        }

        ImGui::BeginChild("StageListChild", ImVec2(0, 0), true);
        for (const auto& pair : worldGroups) {
            if (ImGui::CollapsingHeader(pair.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable(pair.first.c_str(), 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                    ImGui::TableSetupColumn("Models", ImGuiTableColumnFlags_WidthFixed, 55.0f);
                    ImGui::TableSetupColumn("Textures", ImGuiTableColumnFlags_WidthFixed, 65.0f);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (const auto* st : pair.second) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        bool isSel = (selectedStageId == st->id);
                        if (isSel) {
                            ImGui::TextColored(ImVec4(0.28f, 0.85f, 0.55f, 1.0f), "%s", st->id.c_str());
                        } else {
                            ImGui::Text("%s", st->id.c_str());
                        }

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d", st->modelCount);

                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%d", st->textureCount);

                        ImGui::TableSetColumnIndex(3);
                        std::string btnLabel = "Load##" + st->id;
                        if (ImGui::SmallButton(btnLabel.c_str())) {
                            selectedStageId = st->id;
                            if (onStageSelected) {
                                onStageSelected(st->path);
                            }
                        }
                    }
                    ImGui::EndTable();
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace UI
} // namespace OogieEngine
