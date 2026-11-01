#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Viewer/ViewerState.h  —  Application State & Viewer Configuration
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Rendering/GPUMesh.h"
#include "OogieEngine/Rendering/Camera.h"
#include "raylib.h"
#include <string>

namespace OogieEngine {
namespace Viewer {

struct ViewerState {
    Rendering::RenderMode renderMode = Rendering::RenderMode::Textured;
    Rendering::CameraMode cameraMode = Rendering::CameraMode::Orbit;

    bool showGrid = true;
    bool showStageBrowser = true;
    bool showModelInspector = true;
    bool showTextureGallery = false;
    bool showDialoguePanel = false;
    bool showStatsOverlay = true;

    Color clearColor = { 16, 16, 22, 255 };
    float cameraSpeed = 300.0f;
    std::string activeStagePath = "rom/structured_data/stages/0000";
};

} // namespace Viewer
} // namespace OogieEngine
