#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "imgui.h"
#include "rlImGui.h"

#include "PodModel.h"
#include "StageScene.h"
#include "TextureManager.h"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

struct CameraOrbit {
    Vector3 target = { 0.0f, 0.0f, 0.0f };
    float distance = 400.0f;
    float yaw = 45.0f;     // degrees
    float pitch = 25.0f;   // degrees

    Vector3 GetPosition() const {
        float radYaw = yaw * DEG2RAD;
        float radPitch = pitch * DEG2RAD;

        float x = target.x + distance * cosf(radPitch) * sinf(radYaw);
        float y = target.y + distance * sinf(radPitch);
        float z = target.z + distance * cosf(radPitch) * cosf(radYaw);
        return { x, y, z };
    }

    void Update(Camera3D& cam) {
        // Only process mouse navigation if ImGui is not capturing mouse
        if (!ImGui::GetIO().WantCaptureMouse) {
            // Orbit with Right Mouse Button
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                Vector2 delta = GetMouseDelta();
                yaw -= delta.x * 0.4f;
                pitch += delta.y * 0.4f;
                if (pitch > 89.0f) pitch = 89.0f;
                if (pitch < -89.0f) pitch = -89.0f;
            }

            // Pan with Middle Mouse Button
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
                Vector2 delta = GetMouseDelta();
                float panSpeed = distance * 0.0015f;

                float radYaw = yaw * DEG2RAD;
                Vector3 right = { cosf(radYaw), 0.0f, -sinf(radYaw) };
                Vector3 up = { 0.0f, 1.0f, 0.0f };

                target = Vector3Add(target, Vector3Scale(right, -delta.x * panSpeed));
                target = Vector3Add(target, Vector3Scale(up, delta.y * panSpeed));
            }

            // Zoom with Wheel
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                distance -= wheel * (distance * 0.1f);
                if (distance < 1.0f) distance = 1.0f;
            }
        }

        cam.position = GetPosition();
        cam.target = target;
        cam.up = { 0.0f, 1.0f, 0.0f };
    }

    void FrameBounds(const BoundingBox& box) {
        target = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
        float sizeX = box.max.x - box.min.x;
        float sizeY = box.max.y - box.min.y;
        float sizeZ = box.max.z - box.min.z;
        float maxDim = std::max({ sizeX, sizeY, sizeZ });
        if (maxDim < 5.0f) maxDim = 50.0f;
        distance = maxDim * 1.5f;
    }
};

static void ApplyHalloweenTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.10f, 0.10f, 0.14f, 0.92f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.12f, 0.12f, 0.16f, 0.60f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.14f, 0.14f, 0.18f, 0.95f);
    colors[ImGuiCol_Border]                = ImVec4(0.35f, 0.25f, 0.45f, 0.60f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.18f, 0.16f, 0.24f, 0.80f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.40f, 0.20f, 0.55f, 0.60f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.50f, 0.25f, 0.70f, 0.80f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.15f, 0.12f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.35f, 0.18f, 0.48f, 1.00f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.95f, 0.55f, 0.15f, 1.00f); // Pumpkin Orange
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.85f, 0.45f, 0.15f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(1.00f, 0.60f, 0.20f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.30f, 0.18f, 0.42f, 0.80f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.85f, 0.45f, 0.15f, 0.85f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(1.00f, 0.55f, 0.20f, 1.00f);
    colors[ImGuiCol_Header]                = ImVec4(0.32f, 0.20f, 0.45f, 0.70f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.80f, 0.40f, 0.15f, 0.80f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.95f, 0.50f, 0.20f, 1.00f);
    colors[ImGuiCol_Tab]                   = ImVec4(0.20f, 0.16f, 0.26f, 0.86f);
    colors[ImGuiCol_TabHovered]            = ImVec4(0.85f, 0.45f, 0.15f, 0.80f);
    colors[ImGuiCol_TabSelected]           = ImVec4(0.40f, 0.22f, 0.56f, 1.00f);
}

int main(int argc, char* argv[]) {
    const int initialWidth = 1440;
    const int initialHeight = 900;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(initialWidth, initialHeight, "Oogie's Revenge - 3D Stage & Model Viewer");
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ApplyHalloweenTheme();

    Camera3D camera = { 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    CameraOrbit orbit;
    StageScene scene;

    // Discover stages in structured_data/stages
    std::string stagesRootDir = "structured_data/stages";
    std::vector<std::string> availableStages = StageScene::DiscoverStages(stagesRootDir);

    int currentStageIdx = 0;
    if (!availableStages.empty()) {
        scene.LoadStage(stagesRootDir + "/" + availableStages[0]);
        orbit.FrameBounds(scene.GetBoundingBox());
    }

    // View options
    bool wireframe = false;
    bool useTextures = true;
    bool useColors = true;
    bool showGrid = true;
    bool showAxes = true;
    Color bgColor = { 18, 16, 26, 255 }; // Deep dark gothic purple

    // Single model preview mode
    int selectedModelIdx = -1; // -1 for full stage scene

    // Dialogue viewer state
    std::string currentDialogueFile;
    std::vector<std::string> dialogueFiles;
    std::string dialogueContent;

    auto refreshDialogues = [&]() {
        dialogueFiles.clear();
        dialogueContent.clear();
        if (availableStages.empty()) return;
        std::string msgDir = stagesRootDir + "/" + availableStages[currentStageIdx] + "/messages";
        if (fs::exists(msgDir)) {
            for (const auto& entry : fs::directory_iterator(msgDir)) {
                if (entry.path().extension() == ".txt") {
                    dialogueFiles.push_back(entry.path().filename().string());
                }
            }
            std::sort(dialogueFiles.begin(), dialogueFiles.end());
            if (!dialogueFiles.empty()) {
                currentDialogueFile = dialogueFiles[0];
                std::ifstream f(msgDir + "/" + currentDialogueFile);
                std::stringstream ss;
                ss << f.rdbuf();
                dialogueContent = ss.str();
            }
        }
    };
    refreshDialogues();

    while (!WindowShouldClose()) {
        orbit.Update(camera);

        // Render 3D Scene
        BeginDrawing();
        ClearBackground(bgColor);

        BeginMode3D(camera);
        {
            if (showGrid) {
                DrawGrid(60, 20.0f);
            }
            if (showAxes) {
                DrawLine3D({0, 0, 0}, {50, 0, 0}, RED);
                DrawLine3D({0, 0, 0}, {0, 50, 0}, GREEN);
                DrawLine3D({0, 0, 0}, {0, 0, 50}, BLUE);
            }

            if (selectedModelIdx == -1) {
                // Render full stage
                scene.Draw(wireframe, useTextures, useColors);
            } else if (selectedModelIdx >= 0 && selectedModelIdx < (int)scene.GetModelInstances().size()) {
                // Render single selected model
                const auto& inst = scene.GetModelInstances()[selectedModelIdx];
                if (inst.model) {
                    inst.model->Draw(wireframe, useTextures, useColors);
                }
            }
        }
        EndMode3D();

        // Render ImGui UI
        rlImGuiBegin();
        {
            // Main Control Window
            ImGui::SetNextWindowPos(ImVec2(16, 16), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(380, 720), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Stage & Model Explorer", nullptr)) {
                if (ImGui::BeginTabBar("Tabs")) {
                    // TAB 1: Stage & Models
                    if (ImGui::BeginTabItem("Stage Models")) {
                        // Stage Selector Combo
                        if (!availableStages.empty()) {
                            ImGui::Text("Active Stage:");
                            if (ImGui::BeginCombo("##StageCombo", availableStages[currentStageIdx].c_str())) {
                                for (int i = 0; i < (int)availableStages.size(); i++) {
                                    bool isSelected = (currentStageIdx == i);
                                    if (ImGui::Selectable(availableStages[i].c_str(), isSelected)) {
                                        currentStageIdx = i;
                                        selectedModelIdx = -1;
                                        scene.LoadStage(stagesRootDir + "/" + availableStages[currentStageIdx]);
                                        orbit.FrameBounds(scene.GetBoundingBox());
                                        refreshDialogues();
                                    }
                                    if (isSelected) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }

                        ImGui::Spacing();
                        if (ImGui::Button("Reset Camera [F]", ImVec2(-1, 28)) || IsKeyPressed(KEY_F)) {
                            if (selectedModelIdx == -1) {
                                orbit.FrameBounds(scene.GetBoundingBox());
                            } else if (selectedModelIdx >= 0 && selectedModelIdx < (int)scene.GetModelInstances().size()) {
                                orbit.FrameBounds(scene.GetModelInstances()[selectedModelIdx].model->GetBoundingBox());
                            }
                        }

                        ImGui::Separator();
                        ImGui::Text("View Mode:");
                        if (ImGui::RadioButton("Full Level Scene", selectedModelIdx == -1)) {
                            selectedModelIdx = -1;
                            orbit.FrameBounds(scene.GetBoundingBox());
                        }
                        ImGui::SameLine();
                        if (ImGui::RadioButton("Single Model", selectedModelIdx != -1)) {
                            if (selectedModelIdx == -1 && !scene.GetModelInstances().empty()) {
                                selectedModelIdx = 0;
                                orbit.FrameBounds(scene.GetModelInstances()[0].model->GetBoundingBox());
                            }
                        }

                        ImGui::Separator();
                        ImGui::Text("Models in Stage (%d):", (int)scene.GetModelInstances().size());

                        ImGui::BeginChild("ModelList", ImVec2(0, 300), true);
                        auto& instances = scene.GetModelInstances();
                        for (int i = 0; i < (int)instances.size(); i++) {
                            ImGui::PushID(i);
                            bool isCurrent = (selectedModelIdx == i);

                            ImGui::Checkbox("##vis", &instances[i].visible);
                            ImGui::SameLine();

                            if (ImGui::Selectable(instances[i].name.c_str(), isCurrent)) {
                                selectedModelIdx = i;
                                orbit.FrameBounds(instances[i].model->GetBoundingBox());
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndChild();

                        if (selectedModelIdx >= 0 && selectedModelIdx < (int)instances.size()) {
                            const auto& cur = instances[selectedModelIdx];
                            ImGui::Separator();
                            ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "Selected: %s.pod", cur.name.c_str());
                            if (cur.model) {
                                ImGui::Text("Triangles: %d | Vertices: %d", cur.model->GetTotalTriangles(), cur.model->GetTotalVertices());
                                ImGui::Text("Objects: %d | Submeshes: %d", (int)cur.model->GetObjects().size(), cur.model->GetSubmeshCount());

                                const auto& texNames = cur.model->GetTextureNames();
                                if (!texNames.empty()) {
                                    ImGui::Text("Textures (%d):", (int)texNames.size());
                                    for (const auto& t : texNames) {
                                        ImGui::BulletText("%s", t.c_str());
                                    }
                                }
                            }
                        }

                        ImGui::EndTabItem();
                    }

                    // TAB 2: Render Settings
                    if (ImGui::BeginTabItem("Render Options")) {
                        ImGui::Checkbox("Wireframe Mode", &wireframe);
                        ImGui::Checkbox("Enable Textures", &useTextures);
                        ImGui::Checkbox("Enable Vertex Colors", &useColors);
                        ImGui::Separator();
                        ImGui::Checkbox("Draw Grid", &showGrid);
                        ImGui::Checkbox("Draw Axes", &showAxes);

                        ImGui::Separator();
                        float col[3] = { bgColor.r / 255.0f, bgColor.g / 255.0f, bgColor.b / 255.0f };
                        if (ImGui::ColorEdit3("Background Color", col)) {
                            bgColor = { (uint8_t)(col[0] * 255), (uint8_t)(col[1] * 255), (uint8_t)(col[2] * 255), 255 };
                        }

                        ImGui::Separator();
                        ImGui::Text("Performance & Stats:");
                        ImGui::Text("FPS: %d", GetFPS());
                        ImGui::Text("Stage Triangles: %d", scene.GetTotalTriangles());
                        ImGui::Text("Stage Vertices: %d", scene.GetTotalVertices());
                        ImGui::Text("Camera Dist: %.1f", orbit.distance);

                        ImGui::EndTabItem();
                    }

                    // TAB 3: Story & Dialogues (BMS)
                    if (ImGui::BeginTabItem("Dialogues (BMS)")) {
                        if (dialogueFiles.empty()) {
                            ImGui::TextWrapped("No dialogue files found in this stage.");
                        } else {
                            ImGui::Text("Script File:");
                            if (ImGui::BeginCombo("##DiagCombo", currentDialogueFile.c_str())) {
                                for (const auto& df : dialogueFiles) {
                                    bool isSel = (currentDialogueFile == df);
                                    if (ImGui::Selectable(df.c_str(), isSel)) {
                                        currentDialogueFile = df;
                                        std::string p = stagesRootDir + "/" + availableStages[currentStageIdx] + "/messages/" + currentDialogueFile;
                                        std::ifstream f(p);
                                        std::stringstream ss;
                                        ss << f.rdbuf();
                                        dialogueContent = ss.str();
                                    }
                                    if (isSel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }

                            ImGui::Separator();
                            ImGui::Text("Decoded Text:");
                            ImGui::BeginChild("DialogueContent", ImVec2(0, 0), true);
                            ImGui::TextWrapped("%s", dialogueContent.c_str());
                            ImGui::EndChild();
                        }
                        ImGui::EndTabItem();
                    }

                    // TAB 4: Texture Gallery
                    if (ImGui::BeginTabItem("Textures")) {
                        const auto& allTex = TextureManager::Instance().GetAllLoaded();
                        ImGui::Text("Loaded VRAM Textures: %d", (int)allTex.size());
                        ImGui::BeginChild("TexGallery", ImVec2(0, 0), true);
                        for (const auto& [name, tex] : allTex) {
                            if (tex.id > 0) {
                                ImGui::Text("%s (%dx%d)", name.c_str(), tex.width, tex.height);
                                rlImGuiImageSize(&tex, 128, 128);
                                ImGui::Separator();
                            }
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
        }
        rlImGuiEnd();

        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();

    return 0;
}
