#include "OogieEngine/Viewer/ViewerApp.h"
#include "OogieEngine/UI/UITheme.h"
#include "OogieEngine/Rendering/GPUTexture.h"
#include "raylib.h"
#include "rlgl.h"
#include "rlImGui.h"
#include "imgui.h"
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace Viewer {

int ViewerApp::Run(int argc, char** argv) {
    ViewerApp app;
    if (!app.Initialize()) {
        std::cerr << "[ViewerApp] Failed to initialize engine!" << std::endl;
        return -1;
    }

    if (argc > 1) {
        app.LoadStage(argv[1]);
    }

    app.RunLoop();
    app.Shutdown();
    return 0;
}

ViewerApp::ViewerApp() = default;

ViewerApp::~ViewerApp() {
    if (isRunning) {
        Shutdown();
    }
}

bool ViewerApp::Initialize(int width, int height, const char* title) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(width, height, title);
    SetTargetFPS(60);

    rlImGuiSetup(true);
    UI::UITheme::ApplyGothicTheme();
    Rendering::GPUTextureCache::Instance().Initialize();

    isRunning = true;

    // Set far clipping distance to 100,000 units so nothing is clipped
    rlSetClipPlanes(state.nearClip, state.farClip);

    // Default stage to load if found across common candidate paths
    const char* defaultCandidates[] = {
        "rom/structured_data/stages/0000",
        "structured_data/stages/0000",
        "../rom/structured_data/stages/0000",
        "../structured_data/stages/0000"
    };
    for (const char* p : defaultCandidates) {
        if (fs::exists(p)) {
            LoadStage(p);
            break;
        }
    }

    return true;
}

void ViewerApp::Shutdown() {
    if (!isRunning) return;

    stageScene.Unload();
    Rendering::GPUTextureCache::Instance().Shutdown();
    rlImGuiShutdown();
    CloseWindow();
    isRunning = false;
}

void ViewerApp::LoadStage(const std::string& path) {
    state.activeStagePath = path;
    Rendering::GPUTextureCache::Instance().Clear();

    if (stageScene.LoadStage(path)) {
        camera.FocusOn(stageScene.GetBoundingBox());
    }
}

void ViewerApp::ProcessInput(float dt) {
    camera.Update(dt);

    // Quick hotkeys
    if (!ImGui::GetIO().WantCaptureKeyboard) {
        if (IsKeyPressed(KEY_ONE)) state.renderMode = Rendering::RenderMode::Textured;
        if (IsKeyPressed(KEY_TWO)) state.renderMode = Rendering::RenderMode::VertexColor;
        if (IsKeyPressed(KEY_THREE)) state.renderMode = Rendering::RenderMode::Wireframe;
        if (IsKeyPressed(KEY_FOUR)) state.renderMode = Rendering::RenderMode::FlatShaded;
        if (IsKeyPressed(KEY_FIVE)) state.renderMode = Rendering::RenderMode::Normals;
        if (IsKeyPressed(KEY_G)) state.showGrid = !state.showGrid;
        if (IsKeyPressed(KEY_F)) camera.FocusOn(stageScene.GetBoundingBox());
        if (IsKeyPressed(KEY_C)) {
            camera.SetMode(camera.GetMode() == Rendering::CameraMode::Orbit ?
                           Rendering::CameraMode::Fly : Rendering::CameraMode::Orbit);
        }
    }
}

void ViewerApp::RenderScene() {
    ClearBackground(state.clearColor);

    // Apply camera clip planes (farClip = 100,000 units)
    rlSetClipPlanes(state.nearClip, state.farClip);

    BeginMode3D(camera.GetRaylibCamera());

    if (state.showGrid) {
        DrawGrid(100, 50.0f);
    }

    stageScene.Draw(state.renderMode);

    EndMode3D();
}

void ViewerApp::RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Rescan Stages")) {
                stageBrowserPanel.ScanStages("rom/structured_data/stages");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                isRunning = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Stage Browser", nullptr, &state.showStageBrowser);
            ImGui::MenuItem("Model Hierarchy", nullptr, &state.showModelInspector);
            ImGui::MenuItem("Texture Gallery", nullptr, &state.showTextureGallery);
            ImGui::MenuItem("Dialogue Reader", nullptr, &state.showDialoguePanel);
            ImGui::Separator();
            ImGui::MenuItem("HUD Stats", nullptr, &state.showStatsOverlay);
            ImGui::MenuItem("Ground Grid", "G", &state.showGrid);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Render Mode")) {
            if (ImGui::MenuItem("1 - Textured", nullptr, state.renderMode == Rendering::RenderMode::Textured)) {
                state.renderMode = Rendering::RenderMode::Textured;
            }
            if (ImGui::MenuItem("2 - Vertex Colors", nullptr, state.renderMode == Rendering::RenderMode::VertexColor)) {
                state.renderMode = Rendering::RenderMode::VertexColor;
            }
            if (ImGui::MenuItem("3 - Wireframe", nullptr, state.renderMode == Rendering::RenderMode::Wireframe)) {
                state.renderMode = Rendering::RenderMode::Wireframe;
            }
            if (ImGui::MenuItem("4 - Flat Shaded", nullptr, state.renderMode == Rendering::RenderMode::FlatShaded)) {
                state.renderMode = Rendering::RenderMode::FlatShaded;
            }
            if (ImGui::MenuItem("5 - Normals", nullptr, state.renderMode == Rendering::RenderMode::Normals)) {
                state.renderMode = Rendering::RenderMode::Normals;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Camera")) {
            bool isOrbit = (camera.GetMode() == Rendering::CameraMode::Orbit);
            if (ImGui::MenuItem("Orbit Camera (Turntable)", "C", isOrbit)) {
                camera.SetMode(Rendering::CameraMode::Orbit);
            }
            if (ImGui::MenuItem("Fly Camera (WASD+QE)", "C", !isOrbit)) {
                camera.SetMode(Rendering::CameraMode::Fly);
            }
            ImGui::Separator();
            bool invPitch = camera.GetInvertPitch();
            if (ImGui::MenuItem("Invert Pitch (Vertical)", nullptr, invPitch)) {
                camera.SetInvertPitch(!invPitch);
            }
            bool invYaw = camera.GetInvertYaw();
            if (ImGui::MenuItem("Invert Yaw (Horizontal)", nullptr, invYaw)) {
                camera.SetInvertYaw(!invYaw);
            }
            ImGui::Separator();
            float speed = camera.GetMoveSpeed();
            if (ImGui::SliderFloat("Fly Speed", &speed, 50.0f, 3000.0f, "%.0f")) {
                camera.SetMoveSpeed(speed);
            }
            float sens = camera.GetOrbitSensitivity();
            if (ImGui::SliderFloat("Orbit Sens", &sens, 0.05f, 1.5f, "%.2f")) {
                camera.SetOrbitSensitivity(sens);
            }
            ImGui::Separator();
            ImGui::TextDisabled("View Clipping:");
            ImGui::SliderFloat("##FarClip", &state.farClip, 2000.0f, 200000.0f, "Far: %.0f units");
            ImGui::Separator();
            if (ImGui::MenuItem("Focus All", "F")) {
                camera.FocusOn(stageScene.GetBoundingBox());
            }
            if (ImGui::MenuItem("Reset View")) {
                camera.Reset();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void ViewerApp::RenderUI() {
    rlImGuiBegin();

    RenderMenuBar();

    // 1. Stage Browser Panel
    stageBrowserPanel.Draw(&state.showStageBrowser, [this](const std::string& path) {
        LoadStage(path);
    });

    // 2. Model Inspector Panel
    modelInspectorPanel.Draw(&state.showModelInspector, stageScene, [this](const Core::BoundingBox& box) {
        camera.FocusOn(box);
    });

    // 3. Texture Gallery Panel
    textureGalleryPanel.Draw(&state.showTextureGallery, state.activeStagePath);

    // 4. Story & Dialogue Panel
    dialoguePanel.Draw(&state.showDialoguePanel, state.activeStagePath);

    // 5. On-screen HUD Overlay
    if (state.showStatsOverlay) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        ImGui::SetNextWindowPos(ImVec2(10, 35), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.65f);

        if (ImGui::Begin("StatsHUD", nullptr, flags)) {
            ImGui::TextColored(ImVec4(0.28f, 0.85f, 0.55f, 1.0f), "OogieEngine Level Viewer");
            ImGui::Text("FPS: %d (%.2f ms)", GetFPS(), GetFrameTime() * 1000.0f);
            ImGui::Text("Stage: %s", stageScene.GetStageId().c_str());
            ImGui::Text("Models: %d | Verts: %'d | Tris: %'d",
                        stageScene.GetTotalModels(), stageScene.GetTotalVertices(), stageScene.GetTotalTriangles());
            ImGui::TextDisabled("Camera: [%.0f, %.0f, %.0f] | Mode: %s",
                                camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z,
                                camera.GetMode() == Rendering::CameraMode::Orbit ? "Orbit" : "Fly");
            ImGui::TextDisabled("Controls: LMB Orbit | Hold RMB + WASD Fly | MMB / Shift+LMB Pan | Wheel Zoom");
        }
        ImGui::End();
    }

    rlImGuiEnd();
}

void ViewerApp::RunLoop() {
    while (!WindowShouldClose() && isRunning) {
        float dt = GetFrameTime();

        ProcessInput(dt);
        stageScene.Update(dt);

        BeginDrawing();
        RenderScene();
        RenderUI();
        EndDrawing();
    }
}

} // namespace Viewer
} // namespace OogieEngine

