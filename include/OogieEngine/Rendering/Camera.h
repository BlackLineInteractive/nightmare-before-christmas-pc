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

    bool GetInvertPitch() const { return invertPitch; }
    void SetInvertPitch(bool inv) { invertPitch = inv; }

    bool GetInvertYaw() const { return invertYaw; }
    void SetInvertYaw(bool inv) { invertYaw = inv; }

    float GetOrbitSensitivity() const { return orbitSensitivity; }
    void SetOrbitSensitivity(float sens) { orbitSensitivity = sens; }

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

    // Sensitivity & inversion options
    bool invertPitch = false;
    bool invertYaw = false;
    float orbitSensitivity = 0.35f;

    // Fly parameters
    float moveSpeed = 400.0f;
    float lookSpeed = 0.25f;

    bool isDraggingOrbit = false;
    bool isDraggingPan = false;
    bool isDraggingRMBFly = false;

    void UpdateOrbit();
    void UpdateFly(float dt);
};

} // namespace Rendering
} // namespace OogieEngine

