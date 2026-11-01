#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Viewer/ViewerApp.h  —  Main Application Lifecycle & Coordinator
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Viewer/ViewerState.h"
#include "OogieEngine/Rendering/Camera.h"
#include "OogieEngine/SG/StageScene.h"
#include "OogieEngine/UI/Panels/StageBrowserPanel.h"
#include "OogieEngine/UI/Panels/ModelInspectorPanel.h"
#include "OogieEngine/UI/Panels/TextureGalleryPanel.h"
#include "OogieEngine/UI/Panels/DialoguePanel.h"

namespace OogieEngine {
namespace Viewer {

class ViewerApp {
public:
    static int Run(int argc, char** argv);

    ViewerApp();
    ~ViewerApp();

    bool Initialize(int width = 1600, int height = 900, const char* title = "OogieEngine 3D Level Viewer");
    void Shutdown();
    void RunLoop();

    void LoadStage(const std::string& path);

private:
    void ProcessInput(float dt);
    void RenderScene();
    void RenderUI();
    void RenderMenuBar();

    ViewerState state;
    Rendering::ViewerCamera camera;
    SG::StageScene stageScene;

    UI::StageBrowserPanel stageBrowserPanel;
    UI::ModelInspectorPanel modelInspectorPanel;
    UI::TextureGalleryPanel textureGalleryPanel;
    UI::DialoguePanel dialoguePanel;

    bool isRunning = false;
};

} // namespace Viewer
} // namespace OogieEngine

