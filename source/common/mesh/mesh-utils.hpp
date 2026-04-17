#pragma once

#include "mesh.hpp"
#include <glm/vec3.hpp>

#include <string>
#include <vector>

namespace our::mesh_utils {
    // Load an ".obj" file into the mesh
    Mesh* loadOBJ(const std::string& filename);

    // A sub-mesh extracted from an OBJ file based on material usage (usemtl/material_id).
    // diffuseTexturePath is resolved relative to the OBJ's base directory.
    struct OBJSubMesh {
        Mesh* mesh = nullptr;
        std::string diffuseTexturePath;
        glm::vec3 diffuseColor = glm::vec3(1.0f);
        std::string materialName;
    };

    // Load an ".obj" file and split it into multiple meshes, one per material.
    // This is useful for rendering models with multiple textures/materials (like the Sochi track).
    std::vector<OBJSubMesh> loadOBJWithMaterials(const std::string& filename);

    // Create a sphere (the vertex order in the triangles are CCW from the outside)
    // Segments define the number of divisions on the both the latitude and the longitude
    Mesh* sphere(const glm::ivec2& segments);
}