#include "OogieEngine/Rendering/Camera.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>

namespace OogieEngine {
namespace Rendering {

static constexpr float DEG2RAD_F = 0.01745329251994329576923690768489f;

ViewerCamera::ViewerCamera() {
    camera.position = Vector3{ 0.0f, 200.0f, 500.0f };
    camera.target = Vector3{ 0.0f, 50.0f, 0.0f };
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    orbitTarget = camera.target;
    UpdateOrbit();
}

void ViewerCamera::Reset() {
    orbitDistance = 500.0f;
    orbitYaw = 45.0f;
    orbitPitch = 30.0f;
    orbitTarget = Vector3{ 0.0f, 50.0f, 0.0f };
    UpdateOrbit();
}

void ViewerCamera::SetFromStageConfig(const Core::StageConfig& config) {
    camera.position = Vector3{ config.cameraPos.x, config.cameraPos.y, config.cameraPos.z };
    camera.target = Vector3{ 0.0f, config.cameraPos.y * 0.5f, 0.0f };
    orbitTarget = camera.target;

    Vector3 diff = Vector3Subtract(camera.position, orbitTarget);
    orbitDistance = Vector3Length(diff);
    if (orbitDistance < 10.0f) orbitDistance = 500.0f;

    orbitYaw = atan2f(diff.x, diff.z) * (180.0f / 3.14159265f);
    orbitPitch = asinf(std::clamp(diff.y / orbitDistance, -0.99f, 0.99f)) * (180.0f / 3.14159265f);
    UpdateOrbit();
}

void ViewerCamera::FocusOn(const Core::BoundingBox& bbox) {
    Core::Vec3 c = bbox.Center();
    orbitTarget = Vector3{ c.x, c.y, c.z };

    float maxDim = bbox.MaxDimension();
    if (maxDim < 1.0f) maxDim = 50.0f;

    orbitDistance = maxDim * 1.6f;
    if (orbitDistance < 20.0f) orbitDistance = 50.0f;

    orbitYaw = 45.0f;
    orbitPitch = 25.0f;
    UpdateOrbit();
}

void ViewerCamera::UpdateOrbit() {
    float yawRad = orbitYaw * DEG2RAD_F;
    float pitchRad = orbitPitch * DEG2RAD_F;

    float cosP = cosf(pitchRad);
    float sinP = sinf(pitchRad);
    float sinY = sinf(yawRad);
    float cosY = cosf(yawRad);

    camera.target = orbitTarget;
    camera.position = Vector3{
        orbitTarget.x + orbitDistance * cosP * sinY,
        orbitTarget.y + orbitDistance * sinP,
        orbitTarget.z + orbitDistance * cosP * cosY
    };
}

void ViewerCamera::UpdateFly(float dt) {
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
    Vector3 up = camera.up;

    float speed = moveSpeed * dt;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) speed *= 2.5f;

    Vector3 delta = { 0, 0, 0 };
    if (IsKeyDown(KEY_W)) delta = Vector3Add(delta, Vector3Scale(forward, speed));
    if (IsKeyDown(KEY_S)) delta = Vector3Subtract(delta, Vector3Scale(forward, speed));
    if (IsKeyDown(KEY_D)) delta = Vector3Add(delta, Vector3Scale(right, speed));
    if (IsKeyDown(KEY_A)) delta = Vector3Subtract(delta, Vector3Scale(right, speed));
    if (IsKeyDown(KEY_E)) delta = Vector3Add(delta, Vector3Scale(up, speed));
    if (IsKeyDown(KEY_Q)) delta = Vector3Subtract(delta, Vector3Scale(up, speed));

    camera.position = Vector3Add(camera.position, delta);
    camera.target = Vector3Add(camera.target, delta);

    // Mouse look when right mouse is held
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 mouseDelta = GetMouseDelta();
        float yaw = -mouseDelta.x * lookSpeed * DEG2RAD_F;
        float pitch = -mouseDelta.y * lookSpeed * DEG2RAD_F;

        Matrix rot = MatrixMultiply(MatrixRotate(up, yaw), MatrixRotate(right, pitch));
        Vector3 newForward = Vector3Transform(forward, rot);
        camera.target = Vector3Add(camera.position, newForward);
    }
}

void ViewerCamera::Update(float dt) {
    // If mouse is interacting with ImGui UI, don't capture scene camera
    bool imguiActive = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;

    if (mode == CameraMode::Orbit) {
        if (!imguiActive) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                orbitDistance -= wheel * (orbitDistance * 0.12f);
                orbitDistance = std::clamp(orbitDistance, 5.0f, 50000.0f);
                UpdateOrbit();
            }

            // Right click drag: rotate orbit
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsKeyDown(KEY_LEFT_SHIFT)) {
                Vector2 mouseDelta = GetMouseDelta();
                orbitYaw -= mouseDelta.x * 0.4f;
                orbitPitch += mouseDelta.y * 0.4f;
                orbitPitch = std::clamp(orbitPitch, -89.0f, 89.0f);
                UpdateOrbit();
            }

            // Middle click drag or Shift+Right click drag: Pan
            if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && IsKeyDown(KEY_LEFT_SHIFT))) {
                Vector2 mouseDelta = GetMouseDelta();
                Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
                Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
                Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));

                float panSpeed = orbitDistance * 0.0015f;
                Vector3 pan = Vector3Add(Vector3Scale(right, -mouseDelta.x * panSpeed), Vector3Scale(up, mouseDelta.y * panSpeed));
                orbitTarget = Vector3Add(orbitTarget, pan);
                UpdateOrbit();
            }
        }
    } else {
        if (!imguiActive) {
            UpdateFly(dt);
        }
    }
}

} // namespace Rendering
} // namespace OogieEngine
