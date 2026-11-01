#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/UI/Panels/TextureGalleryPanel.h  —  Stage Texture Gallery & Inspector
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

namespace OogieEngine {
namespace UI {

class TextureGalleryPanel {
public:
    TextureGalleryPanel() = default;

    void Draw(bool* pOpen, const std::string& currentStagePath);

private:
    char searchFilter[64] = "";
    float thumbnailSize = 96.0f;
};

} // namespace UI
} // namespace OogieEngine
