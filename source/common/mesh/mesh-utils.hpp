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

    // Load a model file and split it into multiple meshes, one per material.
    // If mergeByMaterial is true, all sub-meshes with the same material are merged across nodes.
    // This is useful for rendering models with multiple textures/materials (like the Sochi track).
    std::vector<ModelSubMesh> loadModelWithMaterials(const std::string& filename, bool mergeByMaterial = false);

    // Create a sphere (the vertex order in the triangles are CCW from the outside)
    // Segments define the number of divisions on the both the latitude and the longitude
    Mesh* sphere(const glm::ivec2& segments);
}