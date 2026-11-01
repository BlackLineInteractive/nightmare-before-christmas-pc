#include "OogieEngine/UI/Panels/TextureGalleryPanel.h"
#include "OogieEngine/Rendering/GPUTexture.h"
#include "rlImGui.h"
#include "imgui.h"
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace UI {

void TextureGalleryPanel::Draw(bool* pOpen, const std::string& currentStagePath) {
    if (!pOpen || !*pOpen) return;

    ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Texture Gallery", pOpen)) {
        if (currentStagePath.empty()) {
            ImGui::TextDisabled("Load a stage to view its textures.");
            ImGui::End();
            return;
        }

        std::string texDir = currentStagePath + "/textures";
        if (!fs::exists(texDir)) {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "No textures directory found at: %s", texDir.c_str());
            ImGui::End();
            return;
        }

        std::vector<std::string> textureFiles;
        for (const auto& entry : fs::directory_iterator(texDir)) {
            if (entry.path().extension() == ".dds" || entry.path().extension() == ".pig") {
                textureFiles.push_back(entry.path().stem().string());
            }
        }
        std::sort(textureFiles.begin(), textureFiles.end());

        ImGui::TextColored(ImVec4(0.28f, 0.85f, 0.55f, 1.0f), "Textures in Stage: %zu", textureFiles.size());
        ImGui::SliderFloat("Thumb Size", &thumbnailSize, 64.0f, 256.0f, "%.0f px");
        ImGui::InputTextWithHint("##TexFilter", "Search textures...", searchFilter, sizeof(searchFilter));
        ImGui::Separator();

        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGuiStyle& style = ImGui::GetStyle();

        ImGui::BeginChild("TextureGrid", ImVec2(0, 0), true);

        for (size_t i = 0; i < textureFiles.size(); i++) {
            const std::string& texName = textureFiles[i];

            if (searchFilter[0] != '\0' && texName.find(searchFilter) == std::string::npos) {
                continue;
            }

            ImGui::PushID(static_cast<int>(i));

            Texture2D tex = Rendering::GPUTextureCache::Instance().GetTexture(texName, texDir);

            ImGui::BeginGroup();
            if (tex.id > 0) {
                rlImGuiImageSize(&tex, static_cast<int>(thumbnailSize), static_cast<int>(thumbnailSize));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("%s (%d x %d)", texName.c_str(), tex.width, tex.height);
                    rlImGuiImageSize(&tex, std::min(tex.width, 300), std::min(tex.height, 300));
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::Dummy(ImVec2(thumbnailSize, thumbnailSize));
            }

            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + thumbnailSize);
            ImGui::TextWrapped("%s", texName.c_str());
            ImGui::TextDisabled("%dx%d", tex.width, tex.height);
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();

            float lastButtonX2 = ImGui::GetItemRectMax().x;
            float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + thumbnailSize;
            if (i + 1 < textureFiles.size() && nextButtonX2 < windowVisibleX2) {
                ImGui::SameLine();
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace UI
} // namespace OogieEngine
