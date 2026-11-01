#include "OogieEngine/UI/Panels/ModelInspectorPanel.h"
#include "imgui.h"

namespace OogieEngine {
namespace UI {

void ModelInspectorPanel::Draw(bool* pOpen, SG::StageScene& scene,
                               const std::function<void(const Core::BoundingBox&)>& onFocusRequested) {
    if (!pOpen || !*pOpen) return;

    ImGui::SetNextWindowSize(ImVec2(350, 480), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Model Hierarchy", pOpen)) {
        if (!scene.IsLoaded()) {
            ImGui::TextDisabled("No stage loaded. Select one in Stage Browser.");
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(0.95f, 0.70f, 0.20f, 1.0f), "Stage: %s", scene.GetStageId().c_str());
        ImGui::Text("Models: %d | Vertices: %'d | Triangles: %'d",
                    scene.GetTotalModels(), scene.GetTotalVertices(), scene.GetTotalTriangles());

        const auto& bb = scene.GetBoundingBox();
        ImGui::TextDisabled("Bounds: [%.0f, %.0f, %.0f] to [%.0f, %.0f, %.0f]",
                            bb.min.x, bb.min.y, bb.min.z, bb.max.x, bb.max.y, bb.max.z);

        if (ImGui::Button("Focus All Scene")) {
            if (onFocusRequested) onFocusRequested(bb);
        }
        ImGui::SameLine();
        if (ImGui::Button("Show All")) {
            for (auto& node : scene.GetModelNodes()) node->SetVisible(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Hide All")) {
            for (auto& node : scene.GetModelNodes()) node->SetVisible(false);
        }

        ImGui::Separator();
        ImGui::InputTextWithHint("##ModelFilter", "Search models...", searchFilter, sizeof(searchFilter));
        ImGui::Separator();

        const auto& nodes = scene.GetModelNodes();
        ImGui::BeginChild("ModelNodesScroll", ImVec2(0, 0), true);

        for (size_t i = 0; i < nodes.size(); i++) {
            const auto& node = nodes[i];
            const std::string& name = node->GetName();

            if (searchFilter[0] != '\0' && name.find(searchFilter) == std::string::npos) {
                continue;
            }

            ImGui::PushID(static_cast<int>(i));

            bool visible = node->IsVisible();
            if (ImGui::Checkbox("##vis", &visible)) {
                node->SetVisible(visible);
            }
            ImGui::SameLine();

            bool isSelected = (selectedIndex == static_cast<int>(i));
            if (ImGui::Selectable(name.c_str(), isSelected, 0, ImVec2(140, 0))) {
                selectedIndex = static_cast<int>(i);
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Focus")) {
                if (onFocusRequested) {
                    onFocusRequested(node->GetWorldBoundingBox());
                }
            }

            if (node->GetModel()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%d tris", node->GetModel()->GetTriangleCount());
            }

            // Expanded details if selected
            if (isSelected && node->GetModel()) {
                ImGui::Indent(20.0f);
                ImGui::TextColored(ImVec4(0.28f, 0.85f, 0.55f, 1.0f), "Submesh & Texture Info:");
                const auto& texList = node->GetModel()->GetTextureList();
                if (texList.empty()) {
                    ImGui::TextDisabled("No texture bindings");
                } else {
                    for (const auto& tex : texList) {
                        ImGui::BulletText("%s", tex.c_str());
                    }
                }
                ImGui::Unindent(20.0f);
            }

            ImGui::PopID();
        }

        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace UI
} // namespace OogieEngine
