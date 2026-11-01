#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OogieEngine/Rendering/Camera.h  —  Interactive 3D Camera Controller
// Supports Orbit & Fly/FreeCam modes, smooth framing and clipping controls.
// ─────────────────────────────────────────────────────────────────────────────

#include "OogieEngine/Core/Types.h"
#include "raylib.h"
#include "raymath.h"

namespace OogieEngine {
namespace Rendering {

enum class CameraMode {
    Orbit = 0,
    Fly
};

class ViewerCamera {
public:
    ViewerCamera();

    void Update(float dt);
    void FocusOn(const Core::BoundingBox& bbox);
    void SetFromStageConfig(const Core::StageConfig& config);
    void Reset();

    const Camera3D& GetRaylibCamera() const { return camera; }
    CameraMode GetMode() const { return mode; }
    void SetMode(CameraMode newMode) { mode = newMode; }

    float GetMoveSpeed() const { return moveSpeed; }
    void SetMoveSpeed(float speed) { moveSpeed = speed; }

    Vector3 GetPosition() const { return camera.position; }
    Vector3 GetTarget() const { return camera.target; }

private:
    Camera3D camera;
    CameraMode mode = CameraMode::Orbit;

    // Orbit parameters
    float orbitDistance = 500.0f;
    float orbitYaw = 45.0f;     // degrees
    float orbitPitch = 30.0f;   // degrees
    Vector3 orbitTarget = { 0.0f, 0.0f, 0.0f };

    // Fly parameters
    float moveSpeed = 300.0f;
    float lookSpeed = 0.2f;

    void UpdateOrbit();
    void UpdateFly(float dt);
};

} // namespace Rendering
} // namespace OogieEngine

