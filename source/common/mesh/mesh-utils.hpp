#pragma once

#include "mesh.hpp"
#include <glm/vec3.hpp>
#include "../texture/texture2d.hpp"

#include <string>
#include <vector>

namespace our::mesh_utils {
    // Load a model file (e.g. .obj, .gltf) into a single mesh
    Mesh* loadModel(const std::string& filename);

    // A sub-mesh extracted from a model file based on material usage.
    // diffuseTexturePath is resolved relative to the model's base directory.
    struct ModelSubMesh {
        Mesh* mesh = nullptr;
        std::string diffuseTexturePath;
        Texture2D* diffuseTexture = nullptr;
        glm::vec3 diffuseColor = glm::vec3(1.0f);

        std::string specularTexturePath;
        Texture2D* specularTexture = nullptr;

        std::string roughnessTexturePath;
        Texture2D* roughnessTexture = nullptr;

        std::string aoTexturePath;
        Texture2D* aoTexture = nullptr;

        std::string emissionTexturePath;
        Texture2D* emissionTexture = nullptr;

        std::string materialName;

        // The name that this submesh came from.
        std::string objectName;

        // Pivot in the original local space. The mesh vertices are shifted so that
        // their positions are relative to this pivot (i.e., mesh is centered at origin).
        glm::vec3 pivot = glm::vec3(0.0f);

        // AABB size in the original local space (before centering).
        glm::vec3 aabbSize = glm::vec3(0.0f);
    };

    struct ModelNode {
        std::string name;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 rotation = glm::vec3(0.0f); // Euler angles in radians
        glm::vec3 scale = glm::vec3(1.0f);
        std::vector<int> meshIndices; // Indices into ModelData::submeshes
        std::vector<ModelNode*> children;
        ~ModelNode() { for (auto c : children) delete c; }
    };

    struct ModelData {
        std::vector<ModelSubMesh> submeshes;
        ModelNode* rootNode = nullptr; // Only populated if preserveHierarchy is true
        ~ModelData() { delete rootNode; }

        // Move semantics
        ModelData() = default;
        ModelData(ModelData&& other) noexcept : submeshes(std::move(other.submeshes)), rootNode(other.rootNode) {
            other.rootNode = nullptr;
        }
        ModelData& operator=(ModelData&& other) noexcept {
            if(this != &other){
                submeshes = std::move(other.submeshes);
                delete rootNode;
                rootNode = other.rootNode;
                other.rootNode = nullptr;
            }
            return *this;
        }
        // Disable copying to prevent double free of rootNode
        ModelData(const ModelData&) = delete;
        ModelData& operator=(const ModelData&) = delete;
    };

    // Load a model file and split it into multiple meshes, one per material.
    // If mergeByMaterial is true, all sub-meshes with the same material are merged across nodes.
    // If preserveHierarchy is true, the scene graph is returned in ModelData::rootNode, and
    // vertices are NOT baked with global transforms. (mergeByMaterial overrides preserveHierarchy).
    ModelData loadModelWithMaterials(const std::string& filename, bool mergeByMaterial = false, bool preserveHierarchy = false);

    // Create a sphere (the vertex order in the triangles are CCW from the outside)
    // Segments define the number of divisions on the both the latitude and the longitude
    Mesh* sphere(const glm::ivec2& segments);
}