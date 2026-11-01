#include "OogieEngine/Loader/PodLoader.h"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace OogieEngine {
namespace Loader {

static Core::Vec3 Sub(const Core::Vec3 &a, const Core::Vec3 &b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static Core::Vec3 Cross(const Core::Vec3 &a, const Core::Vec3 &b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static Core::Vec3 Normalize(const Core::Vec3 &v) {
  float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len > 1e-6f) {
    return {v.x / len, v.y / len, v.z / len};
  }
  return {0.0f, 1.0f, 0.0f};
}

static Core::Vec3 Transform(const Core::Vec3 &v, const Core::Mat4 &m) {
  return {v.x * m.m[0] + v.y * m.m[4] + v.z * m.m[8] + m.m[12],
          v.x * m.m[1] + v.y * m.m[5] + v.z * m.m[9] + m.m[13],
          v.x * m.m[2] + v.y * m.m[6] + v.z * m.m[10] + m.m[14]};
}

bool PodLoader::LoadTxi(const std::string &txiPath,
                        std::vector<std::string> &outTextureNames) {
  outTextureNames.clear();
  std::ifstream file(txiPath, std::ios::binary);
  if (!file.is_open())
    return false;

  uint32_t count = 0;
  file.read(reinterpret_cast<char *>(&count), 4);
  if (count == 0 || count > 256)
    return false;

  std::vector<uint32_t> offsets(count);
  file.read(reinterpret_cast<char *>(offsets.data()), count * 4);

  for (size_t i = 0; i < count; i++) {
    file.seekg(offsets[i], std::ios::beg);
    std::string name;
    char c;
    while (file.get(c) && c != '\0') {
      name += c;
    }
    if (!name.empty()) {
      outTextureNames.push_back(name);
    }
  }
  return true;
}

bool PodLoader::Load(const std::string &podPath, Core::ModelData &outModel,
                     const std::string &txiPath) {
  outModel = Core::ModelData();
  outModel.name = fs::path(podPath).stem().string();

  std::string actualTxi = txiPath;
  if (actualTxi.empty()) {
    actualTxi =
        fs::path(podPath).parent_path().string() + "/" + outModel.name + ".txi";
  }
  if (fs::exists(actualTxi)) {
    LoadTxi(actualTxi, outModel.textureList);
  }

  std::ifstream file(podPath, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "PodLoader: Cannot open " << podPath << std::endl;
    return false;
  }

  file.seekg(0, std::ios::end);
  size_t fileSize = file.tellg();
  file.seekg(0, std::ios::beg);

  if (fileSize < 12)
    return false;

  std::vector<uint8_t> buffer(fileSize);
  file.read(reinterpret_cast<char *>(buffer.data()), fileSize);

  if (memcmp(buffer.data(), "P2OD", 4) != 0) {
    std::cerr << "PodLoader: Invalid P2OD signature in " << podPath
              << std::endl;
    return false;
  }

  uint32_t numObjects = *reinterpret_cast<uint32_t *>(&buffer[8]);

  for (uint32_t objIdx = 0; objIdx < numObjects; objIdx++) {
    size_t objHdrOffset = 0x20 + objIdx * 0x50;
    if (objHdrOffset + 0x50 > fileSize)
      break;

    uint32_t meshOffset =
        *reinterpret_cast<uint32_t *>(&buffer[objHdrOffset + 8]);
    const float *matFloats =
        reinterpret_cast<const float *>(&buffer[objHdrOffset + 16]);

    Core::ModelObject obj;
    std::copy(matFloats, matFloats + 16, obj.transform.m.begin());
    // Adjust matrix for inverted Y: S_y * M * S_y
    obj.transform.m[1] = -obj.transform.m[1];
    obj.transform.m[4] = -obj.transform.m[4];
    obj.transform.m[6] = -obj.transform.m[6];
    obj.transform.m[9] = -obj.transform.m[9];
    obj.transform.m[13] = -obj.transform.m[13];

    if (meshOffset + 64 > fileSize)
      continue;

    uint32_t numSubmeshes =
        *reinterpret_cast<uint32_t *>(&buffer[meshOffset + 48]);
    uint32_t defaultTexIdx =
        *reinterpret_cast<uint32_t *>(&buffer[meshOffset + 56]);

    size_t curSubOffset = meshOffset + 0x90;

    for (uint32_t subIdx = 0; subIdx < numSubmeshes; subIdx++) {
      if (curSubOffset + 16 > fileSize)
        break;

      uint32_t numVerts = *reinterpret_cast<uint32_t *>(&buffer[curSubOffset]);
      if (numVerts < 3) {
        curSubOffset += 16 + numVerts * 48;
        continue;
      }

      size_t v_off = curSubOffset + 16;
      size_t c_off = v_off + numVerts * 16;
      size_t uv_off = c_off + numVerts * 16;

      if (uv_off + numVerts * 16 > fileSize)
        break;

      std::vector<Core::Vertex> stripVerts(numVerts);
      std::vector<uint32_t> stripFlags(numVerts);
      for (uint32_t vi = 0; vi < numVerts; vi++) {
        const float *p =
            reinterpret_cast<const float *>(&buffer[v_off + vi * 16]);
        uint32_t flag =
            *reinterpret_cast<const uint32_t *>(&buffer[v_off + vi * 16 + 12]);
        const float *col =
            reinterpret_cast<const float *>(&buffer[c_off + vi * 16]);
        const float *uv =
            reinterpret_cast<const float *>(&buffer[uv_off + vi * 16]);

        Core::Vertex v;
        // Capcom RD3 coordinate system has negative Y pointing up -> Invert to
        // standard OpenGL Y-up
        v.pos = {p[0], -p[1], p[2]};
        v.uv = {uv[0], uv[1]};
        v.color = {std::clamp(col[0] / 255.0f, 0.0f, 1.0f),
                   std::clamp(col[1] / 255.0f, 0.0f, 1.0f),
                   std::clamp(col[2] / 255.0f, 0.0f, 1.0f),
                   std::clamp((col[3] * 2.0f) / 255.0f, 0.0f, 1.0f)};
        stripVerts[vi] = v;
        stripFlags[vi] = flag;
      }

      Core::SubMeshChunk submesh;
      submesh.textureIndex = static_cast<int>(defaultTexIdx);
      if (defaultTexIdx < outModel.textureList.size()) {
        submesh.textureName = outModel.textureList[defaultTexIdx];
      }

        // Capcom RD3 triangle strip expansion using 0x8000 emit flag
        int stripStep = 0;
        for (size_t vi = 0; vi < stripVerts.size(); vi++) {
          uint32_t flag = stripFlags[vi];
          if ((flag & 0x8000) == 0) {
            stripStep = 0; // Strip restart flag
          } else {
            stripStep++;
            if (vi >= 2 && stripStep >= 1) {
              Core::Vertex v0, v1, v2;
              // Alternate winding for Y-inverted strip
              if (stripStep % 2 == 1) {
                v0 = stripVerts[vi - 2];
                v1 = stripVerts[vi];
                v2 = stripVerts[vi - 1];
              } else {
                v0 = stripVerts[vi - 1];
                v1 = stripVerts[vi];
                v2 = stripVerts[vi - 2];
              }

              // Skip degenerate triangles
              if (v0.pos == v1.pos || v1.pos == v2.pos || v0.pos == v2.pos) {
                continue;
              }

                // Compute surface normal pointing outwards
                Core::Vec3 norm =
                    Normalize(Cross(Sub(v1.pos, v0.pos), Sub(v2.pos, v0.pos)));
                v0.normal = norm;
                v1.normal = norm;
                v2.normal = norm;

                submesh.vertices.push_back(v0);
                submesh.vertices.push_back(v1);
                submesh.vertices.push_back(v2);
              }
            }
          }

          obj.submeshes.push_back(std::move(submesh));
          curSubOffset += 16 + numVerts * 48;
        }

        outModel.objects.push_back(std::move(obj));
      }

      ComputeBounds(outModel);
      return !outModel.objects.empty();
    }

    void PodLoader::ComputeBounds(Core::ModelData & model) {
      bool first = true;
      for (auto &obj : model.objects) {
        bool objFirst = true;
        for (auto &sub : obj.submeshes) {
          bool subFirst = true;
          for (const auto &v : sub.vertices) {
            // Local bbox
            if (subFirst) {
              sub.bbox.min = v.pos;
              sub.bbox.max = v.pos;
              subFirst = false;
            } else {
              sub.bbox.min.x = std::min(sub.bbox.min.x, v.pos.x);
              sub.bbox.min.y = std::min(sub.bbox.min.y, v.pos.y);
              sub.bbox.min.z = std::min(sub.bbox.min.z, v.pos.z);

              sub.bbox.max.x = std::max(sub.bbox.max.x, v.pos.x);
              sub.bbox.max.y = std::max(sub.bbox.max.y, v.pos.y);
              sub.bbox.max.z = std::max(sub.bbox.max.z, v.pos.z);
            }

            // World transformed bbox
            Core::Vec3 worldPos = Transform(v.pos, obj.transform);
            if (first) {
              model.bbox.min = worldPos;
              model.bbox.max = worldPos;
              first = false;
            } else {
              model.bbox.min.x = std::min(model.bbox.min.x, worldPos.x);
              model.bbox.min.y = std::min(model.bbox.min.y, worldPos.y);
              model.bbox.min.z = std::min(model.bbox.min.z, worldPos.z);

              model.bbox.max.x = std::max(model.bbox.max.x, worldPos.x);
              model.bbox.max.y = std::max(model.bbox.max.y, worldPos.y);
              model.bbox.max.z = std::max(model.bbox.max.z, worldPos.z);
            }
          }
        }
      }
    }

  } // namespace Loader
} // namespace OogieEngine
