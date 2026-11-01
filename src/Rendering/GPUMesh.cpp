#include "OogieEngine/Rendering/GPUMesh.h"
#include "OogieEngine/Rendering/GPUTexture.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>

namespace OogieEngine {
namespace Rendering {

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

void GPUModel::Draw(RenderMode mode, Matrix worldTransform) const {
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(worldTransform));

    const Vector3 lightDir = Vector3Normalize(Vector3{ 0.4f, 0.9f, 0.3f });

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
                    switch (mode) {
                        case RenderMode::Textured: {
                            float dot = std::max(0.35f, v.normal.x * lightDir.x + v.normal.y * lightDir.y + v.normal.z * lightDir.z);
                            float shade = dot * 0.35f + 0.65f;
                            rlColor4f(shade, shade, shade, 1.0f);
                            break;
                        }
                        case RenderMode::VertexColor:
                            rlColor4f(v.color.x, v.color.y, v.color.z, v.color.w);
                            break;
                        case RenderMode::FlatShaded: {
                            float dot = std::max(0.15f, v.normal.x * lightDir.x + v.normal.y * lightDir.y + v.normal.z * lightDir.z);
                            rlColor4f(dot * 0.85f + 0.15f, dot * 0.85f + 0.15f, dot * 0.85f + 0.15f, 1.0f);
                            break;
                        }
                        case RenderMode::Normals: {
                            float r = v.normal.x * 0.5f + 0.5f;
                            float g = v.normal.y * 0.5f + 0.5f;
                            float b = v.normal.z * 0.5f + 0.5f;
                            rlColor4f(r, g, b, 1.0f);
                            break;
                        }
                        default:
                            rlColor4ub(220, 220, 220, 255);
                            break;
                    }

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
}

} // namespace Rendering
} // namespace OogieEngine

