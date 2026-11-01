#include "OogieEngine/Rendering/GPUMesh.h"
#include "OogieEngine/Rendering/GPUTexture.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

namespace OogieEngine {
namespace Rendering {

static Shader s_alphaShader = { 0 };
static int s_renderModeLoc = -1;
static int s_isAdditiveLoc = -1;

static void EnsureAlphaShader() {
    if (s_alphaShader.id > 0) return;

    const char* vs = R"(
#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
in vec3 vertexNormal;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

    const char* fs = R"(
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform int uRenderMode;
uniform int uIsAdditive;

out vec4 finalColor;

void main()
{
    vec3 lightDir = normalize(vec3(0.4, 0.9, 0.3));
    float diff = max(dot(fragNormal, lightDir), 0.0);
    float light = diff * 0.45 + 0.55;

    if (uRenderMode == 0) {
        vec4 texColor = texture(texture0, fragTexCoord);
        if (uIsAdditive == 1) {
            // Additive mode for spotlights, glows, glares: black adds nothing
            // Modulate with vertex color so perimeter vertices at (0,0,0) fade seamlessly to black
            vec3 vCol = length(fragColor.rgb) > 0.01 ? fragColor.rgb : vec3(1.0);
            vec3 addColor = texColor.rgb * vCol;
            addColor = max(addColor - vec3(0.04), vec3(0.0));
            finalColor = vec4(addColor, 1.0);
        } else {
            if (texColor.a < 0.20) discard; // Alpha cutout: discards transparent pixels completely
            finalColor = vec4(texColor.rgb * colDiffuse.rgb * light, texColor.a * colDiffuse.a);
        }
    }
    else if (uRenderMode == 1) {
        finalColor = vec4(fragColor.rgb * light, 1.0);
    }
    else if (uRenderMode == 3) {
        finalColor = vec4(vec3(light), 1.0);
    }
    else if (uRenderMode == 4) {
        finalColor = vec4(fragNormal * 0.5 + 0.5, 1.0);
    }
    else {
        finalColor = vec4(0.85, 0.85, 0.85, 1.0);
    }
}
)";

    s_alphaShader = LoadShaderFromMemory(vs, fs);
    s_renderModeLoc = GetShaderLocation(s_alphaShader, "uRenderMode");
    s_isAdditiveLoc = GetShaderLocation(s_alphaShader, "uIsAdditive");
}

GPUModel::~GPUModel() {
    Unload();
}

void GPUModel::Unload() {
    objects.clear();
    textures.clear();
    triangleCount = 0;
    vertexCount = 0;
    name.clear();
}

bool GPUModel::Upload(const Core::ModelData& cpuData, const std::string& textureDir) {
    Unload();

    name = cpuData.name;
    textures = cpuData.textureList;
    bbox = cpuData.bbox;
    triangleCount = cpuData.GetTotalTriangles();
    vertexCount = cpuData.GetTotalVertices();

    for (const auto& objData : cpuData.objects) {
        GPUObject gpuObj;

        // Direct copy of Core::Mat4 to Raylib Matrix (both 16 floats column-major)
        std::memcpy(&gpuObj.transform, objData.transform.m.data(), 16 * sizeof(float));

        for (const auto& subData : objData.submeshes) {
            GPUSubMesh gpuSub;
            gpuSub.vertices = subData.vertices;
            gpuSub.textureName = subData.textureName;

            if (!subData.textureName.empty()) {
                gpuSub.texture = GPUTextureCache::Instance().GetTexture(subData.textureName, textureDir);
            } else {
                gpuSub.texture = GPUTextureCache::Instance().GetDefaultTexture();
            }

            gpuObj.submeshes.push_back(std::move(gpuSub));
        }

        objects.push_back(std::move(gpuObj));
    }

    return true;
}

void GPUModel::Draw(RenderMode mode, Matrix worldTransform, bool isAdditive) const {
    EnsureAlphaShader();

    rlDisableBackfaceCulling();
    rlEnableColorBlend();

    int modeInt = static_cast<int>(mode);
    if (s_renderModeLoc != -1) {
        SetShaderValue(s_alphaShader, s_renderModeLoc, &modeInt, SHADER_UNIFORM_INT);
    }

    int addInt = isAdditive ? 1 : 0;
    if (s_isAdditiveLoc != -1) {
        SetShaderValue(s_alphaShader, s_isAdditiveLoc, &addInt, SHADER_UNIFORM_INT);
    }

    BeginShaderMode(s_alphaShader);
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(worldTransform));

    for (const auto& obj : objects) {
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(obj.transform));

        for (const auto& sub : obj.submeshes) {
            if (sub.vertices.empty()) continue;

            if (mode == RenderMode::Textured && sub.texture.id > 0) {
                rlSetTexture(sub.texture.id);
            } else {
                rlSetTexture(0);
            }

            if (mode == RenderMode::Wireframe) {
                rlBegin(RL_LINES);
                rlColor4ub(100, 220, 255, 255);
                for (size_t i = 0; i < sub.vertices.size(); i += 3) {
                    const auto& v0 = sub.vertices[i];
                    const auto& v1 = sub.vertices[i + 1];
                    const auto& v2 = sub.vertices[i + 2];

                    rlVertex3f(v0.pos.x, v0.pos.y, v0.pos.z);
                    rlVertex3f(v1.pos.x, v1.pos.y, v1.pos.z);

                    rlVertex3f(v1.pos.x, v1.pos.y, v1.pos.z);
                    rlVertex3f(v2.pos.x, v2.pos.y, v2.pos.z);

                    rlVertex3f(v2.pos.x, v2.pos.y, v2.pos.z);
                    rlVertex3f(v0.pos.x, v0.pos.y, v0.pos.z);
                }
                rlEnd();
            } else {
                rlBegin(RL_TRIANGLES);
                for (const auto& v : sub.vertices) {
                    rlColor4f(v.color.x, v.color.y, v.color.z, v.color.w);
                    rlNormal3f(v.normal.x, v.normal.y, v.normal.z);
                    rlTexCoord2f(v.uv.x, v.uv.y);
                    rlVertex3f(v.pos.x, v.pos.y, v.pos.z);
                }
                rlEnd();
            }
        }
        rlPopMatrix();
    }

    rlSetTexture(0);
    rlPopMatrix();
    EndShaderMode();
}

} // namespace Rendering
} // namespace OogieEngine

