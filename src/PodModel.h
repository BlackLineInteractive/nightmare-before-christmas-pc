#pragma once

#include "raylib.h"
#include "raymath.h"
#include <string>
#include <vector>

struct PodVertex {
    Vector3 position;
    Vector3 normal;
    Vector2 texcoord;
    Color color;
};

struct PodSubMesh {
    std::vector<PodVertex> vertices;
    std::string textureName;
    Texture2D texture;
    BoundingBox bbox;
};

struct PodObject {
    Matrix transform;
    std::vector<PodSubMesh> submeshes;
    BoundingBox bbox;
};

class PodModel {
public:
    PodModel();
    ~PodModel();

    bool Load(const std::string& podPath, const std::string& textureDir = "");
    void Unload();

    void Draw(bool wireframe, bool useTextures, bool useColors, Matrix worldTransform = MatrixIdentity()) const;

    BoundingBox GetBoundingBox() const { return overallBBox; }
    Vector3 GetCenter() const;

    const std::string& GetName() const { return modelName; }
    const std::vector<PodObject>& GetObjects() const { return objects; }
    const std::vector<std::string>& GetTextureNames() const { return textureList; }

    int GetTotalVertices() const { return totalVertices; }
    int GetTotalTriangles() const { return totalTriangles; }
    int GetSubmeshCount() const { return totalSubmeshes; }

private:
    std::string modelName;
    std::string baseDir;
    std::vector<PodObject> objects;
    std::vector<std::string> textureList;
    BoundingBox overallBBox;

    int totalVertices = 0;
    int totalTriangles = 0;
    int totalSubmeshes = 0;

    void LoadTxi(const std::string& txiPath);
    void ComputeOverallBounds();
};

