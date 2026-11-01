#include "PodModel.h"
#include "TextureManager.h"
#include "rlgl.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <cmath>

namespace fs = std::filesystem;

PodModel::PodModel() {
    overallBBox.min = {0, 0, 0};
    overallBBox.max = {0, 0, 0};
}

PodModel::~PodModel() {
    Unload();
}

void PodModel::Unload() {
    objects.clear();
    textureList.clear();
    totalVertices = 0;
    totalTriangles = 0;
    totalSubmeshes = 0;
    overallBBox.min = {0, 0, 0};
    overallBBox.max = {0, 0, 0};
}

void PodModel::LoadTxi(const std::string& txiPath) {
    textureList.clear();
    std::ifstream f(txiPath, std::ios::binary);
    if (!f.is_open()) return;

    uint32_t count = 0;
    f.read(reinterpret_cast<char*>(&count), 4);
    if (count == 0 || count > 256) return;

    std::vector<uint32_t> offsets(count);
    f.read(reinterpret_cast<char*>(offsets.data()), count * 4);

    f.seekg(0, std::ios::end);
    size_t fileSize = f.tellg();

    for (size_t i = 0; i < count; i++) {
        f.seekg(offsets[i], std::ios::beg);
        std::string name;
        char c;
        while (f.get(c) && c != '\0') {
            name += c;
        }
        if (!name.empty()) {
            textureList.push_back(name);
        }
    }
}

Vector3 PodModel::GetCenter() const {
    return Vector3Scale(Vector3Add(overallBBox.min, overallBBox.max), 0.5f);
}

void PodModel::ComputeOverallBounds() {
    if (objects.empty()) {
        overallBBox = {{0,0,0}, {0,0,0}};
        return;
    }

    bool first = true;
    for (const auto& obj : objects) {
        for (const auto& sub : obj.submeshes) {
            for (const auto& v : sub.vertices) {
                Vector3 worldPos = Vector3Transform(v.position, obj.transform);
                if (first) {
                    overallBBox.min = worldPos;
                    overallBBox.max = worldPos;
                    first = false;
                } else {
                    overallBBox.min.x = std::min(overallBBox.min.x, worldPos.x);
                    overallBBox.min.y = std::min(overallBBox.min.y, worldPos.y);
                    overallBBox.min.z = std::min(overallBBox.min.z, worldPos.z);

                    overallBBox.max.x = std::max(overallBBox.max.x, worldPos.x);
                    overallBBox.max.y = std::max(overallBBox.max.y, worldPos.y);
                    overallBBox.max.z = std::max(overallBBox.max.z, worldPos.z);
                }
            }
        }
    }
}

bool PodModel::Load(const std::string& podPath, const std::string& textureDir) {
    Unload();
    modelName = fs::path(podPath).stem().string();
    baseDir = fs::path(podPath).parent_path().string();

    std::string txiPath = baseDir + "/" + modelName + ".txi";
    if (fs::exists(txiPath)) {
        LoadTxi(txiPath);
    }

    std::ifstream file(podPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open POD file: " << podPath << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);

    if (fileSize < 12 || memcmp(data.data(), "P2OD", 4) != 0) {
        std::cerr << "Invalid P2OD magic: " << podPath << std::endl;
        return false;
    }

    uint32_t headerSize = *reinterpret_cast<uint32_t*>(&data[4]);
    uint32_t numObjects = *reinterpret_cast<uint32_t*>(&data[8]);

    std::string texSearchDir = textureDir.empty() ? baseDir : textureDir;

    for (uint32_t i = 0; i < numObjects; i++) {
        size_t objHdrOffset = 0x20 + i * 0x50;
        if (objHdrOffset + 0x50 > fileSize) break;

        uint32_t meshOffset = *reinterpret_cast<uint32_t*>(&data[objHdrOffset + 8]);
        const float* m = reinterpret_cast<const float*>(&data[objHdrOffset + 16]);

        PodObject obj;
        obj.transform = {
            m[0], m[1], m[2], m[3],
            m[4], m[5], m[6], m[7],
            m[8], m[9], m[10], m[11],
            m[12], m[13], m[14], m[15]
        };

        if (meshOffset + 64 > fileSize) continue;

        uint32_t numSubmeshes = *reinterpret_cast<uint32_t*>(&data[meshOffset + 48]);
        uint32_t defaultTexIdx = *reinterpret_cast<uint32_t*>(&data[meshOffset + 56]);

        size_t curSubOffset = meshOffset + 0x90;

        for (uint32_t sub_i = 0; sub_i < numSubmeshes; sub_i++) {
            if (curSubOffset + 16 > fileSize) break;

            uint32_t numVerts = *reinterpret_cast<uint32_t*>(&data[curSubOffset]);
            if (numVerts < 3) {
                curSubOffset += 16 + numVerts * 48;
                continue;
            }

            size_t v_off = curSubOffset + 16;
            size_t c_off = v_off + numVerts * 16;
            size_t uv_off = c_off + numVerts * 16;

            if (uv_off + numVerts * 16 > fileSize) break;

            std::vector<PodVertex> stripVerts(numVerts);
            for (uint32_t vi = 0; vi < numVerts; vi++) {
                const float* pos = reinterpret_cast<const float*>(&data[v_off + vi * 16]);
                const float* col = reinterpret_cast<const float*>(&data[c_off + vi * 16]);
                const float* uv = reinterpret_cast<const float*>(&data[uv_off + vi * 16]);

                PodVertex vert;
                vert.position = { pos[0], pos[1], pos[2] };
                vert.normal = { 0, 1, 0 }; // computed after triangle expansion
                vert.texcoord = { uv[0], uv[1] };

                uint8_t r = static_cast<uint8_t>(std::clamp(col[0], 0.0f, 255.0f));
                uint8_t g = static_cast<uint8_t>(std::clamp(col[1], 0.0f, 255.0f));
                uint8_t b = static_cast<uint8_t>(std::clamp(col[2], 0.0f, 255.0f));
                uint8_t a = static_cast<uint8_t>(std::clamp(col[3] * 2.0f, 0.0f, 255.0f)); // scale 128 -> 255
                vert.color = { r, g, b, a };

                stripVerts[vi] = vert;
            }

            PodSubMesh submesh;
            if (defaultTexIdx < textureList.size()) {
                submesh.textureName = textureList[defaultTexIdx];
            }
            submesh.texture = TextureManager::Instance().GetTexture(submesh.textureName, texSearchDir);

            // Convert triangle strip to triangle list
            for (size_t vi = 0; vi + 2 < stripVerts.size(); vi++) {
                auto v0 = stripVerts[vi];
                auto v1 = stripVerts[vi + 1];
                auto v2 = stripVerts[vi + 2];

                // Skip degenerate triangles used for strip stitching
                bool degen = (Vector3Equals(v0.position, v1.position) ||
                              Vector3Equals(v1.position, v2.position) ||
                              Vector3Equals(v0.position, v2.position));
                if (degen) continue;

                // Alternate winding for triangle strips
                if (vi % 2 != 0) {
                    std::swap(v0, v1);
                }

                // Compute normal
                Vector3 e1 = Vector3Subtract(v1.position, v0.position);
                Vector3 e2 = Vector3Subtract(v2.position, v0.position);
                Vector3 n = Vector3Normalize(Vector3CrossProduct(e1, e2));
                v0.normal = n;
                v1.normal = n;
                v2.normal = n;

                submesh.vertices.push_back(v0);
                submesh.vertices.push_back(v1);
                submesh.vertices.push_back(v2);
            }

            totalVertices += submesh.vertices.size();
            totalTriangles += submesh.vertices.size() / 3;
            totalSubmeshes++;

            obj.submeshes.push_back(std::move(submesh));
            curSubOffset += 16 + numVerts * 48;
        }

        objects.push_back(std::move(obj));
    }

    ComputeOverallBounds();
    return true;
}

void PodModel::Draw(bool wireframe, bool useTextures, bool useColors, Matrix worldTransform) const {
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(worldTransform));

    for (const auto& obj : objects) {
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(obj.transform));

        for (const auto& sub : obj.submeshes) {
            if (sub.vertices.empty()) continue;

            if (useTextures && sub.texture.id > 0) {
                rlSetTexture(sub.texture.id);
            } else {
                rlSetTexture(0);
            }

            if (wireframe) {
                rlBegin(RL_LINES);
                rlColor4ub(100, 220, 255, 255);
                for (size_t i = 0; i < sub.vertices.size(); i += 3) {
                    const auto& v0 = sub.vertices[i];
                    const auto& v1 = sub.vertices[i + 1];
                    const auto& v2 = sub.vertices[i + 2];

                    rlVertex3f(v0.position.x, v0.position.y, v0.position.z);
                    rlVertex3f(v1.position.x, v1.position.y, v1.position.z);

                    rlVertex3f(v1.position.x, v1.position.y, v1.position.z);
                    rlVertex3f(v2.position.x, v2.position.y, v2.position.z);

                    rlVertex3f(v2.position.x, v2.position.y, v2.position.z);
                    rlVertex3f(v0.position.x, v0.position.y, v0.position.z);
                }
                rlEnd();
            } else {
                rlBegin(RL_TRIANGLES);
                for (const auto& v : sub.vertices) {
                    if (useColors) {
                        rlColor4ub(v.color.r, v.color.g, v.color.b, v.color.a);
                    } else {
                        rlColor4ub(220, 220, 220, 255);
                    }

                    rlNormal3f(v.normal.x, v.normal.y, v.normal.z);
                    rlTexCoord2f(v.texcoord.x, v.texcoord.y);
                    rlVertex3f(v.position.x, v.position.y, v.position.z);
                }
                rlEnd();
            }
        }
        rlPopMatrix();
    }

    rlSetTexture(0);
    rlPopMatrix();
}

