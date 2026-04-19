#include "mesh-utils.hpp"

// We will use "Tiny OBJ Loader" to read and process '.obj" files
#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobj/tiny_obj_loader.h>

#include <iostream>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <limits>

namespace {
    struct SubMeshBuild {
        std::vector<our::Vertex> vertices;
        std::vector<GLuint> elements;
        std::unordered_map<our::Vertex, GLuint> vertex_map;
    };

    struct SubMeshEntry {
        SubMeshBuild build;
        std::string objectName;
        int materialId = -1;
    };
}

our::Mesh* our::mesh_utils::loadOBJ(const std::string& filename) {

    // The data that we will use to initialize our mesh
    std::vector<our::Vertex> vertices;
    std::vector<GLuint> elements;

    // Since the OBJ can have duplicated vertices, we make them unique using this map
    // The key is the vertex, the value is its index in the vector "vertices".
    // That index will be used to populate the "elements" vector.
    std::unordered_map<our::Vertex, GLuint> vertex_map;

    // The data loaded by Tiny OBJ Loader
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string base_dir;
    {
        std::filesystem::path p(filename);
        auto parent = p.parent_path();
        base_dir = parent.empty() ? std::string("./") : (parent.string() + std::string("/"));
    }

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), base_dir.c_str(), true)) {
        std::cerr << "Failed to load obj file \"" << filename << "\" due to error: " << err << std::endl;
        return nullptr;
    }
    if (!warn.empty()) {
        std::cout << "WARN while loading obj file \"" << filename << "\": " << warn << std::endl;
    }

    // An obj file can have multiple shapes where each shape can have its own material
    // Ideally, we would load each shape into a separate mesh or store the start and end of it in the element buffer to be able to draw each shape separately
    // But we ignored this fact since we don't plan to use multiple materials in the examples
    for (const auto &shape : shapes) {
        for (const auto &index : shape.mesh.indices) {
            Vertex vertex = {};

            // Read the data for a vertex from the "attrib" object
            vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
            };

            if(index.normal_index >= 0 && (3 * index.normal_index + 2) < (int)attrib.normals.size()){
                vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                };
            } else {
                vertex.normal = {0, 0, 1};
            }

            if(index.texcoord_index >= 0 && (2 * index.texcoord_index + 1) < (int)attrib.texcoords.size()){
                vertex.tex_coord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        attrib.texcoords[2 * index.texcoord_index + 1]
                };
            } else {
                vertex.tex_coord = {0, 0};
            }

            if((3 * index.vertex_index + 2) < (int)attrib.colors.size()){
                float r = attrib.colors[3 * index.vertex_index + 0] * 255.0f;
                float g = attrib.colors[3 * index.vertex_index + 1] * 255.0f;
                float b = attrib.colors[3 * index.vertex_index + 2] * 255.0f;
                vertex.color = {
                        (glm::uint8)std::clamp(r, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(g, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(b, 0.0f, 255.0f),
                        255
                };
            } else {
                vertex.color = {255, 255, 255, 255};
            }

            // See if we already stored a similar vertex
            auto it = vertex_map.find(vertex);
            if (it == vertex_map.end()) {
                // if no, add it to the vertices and record its index
                auto new_vertex_index = static_cast<GLuint>(vertices.size());
                vertex_map[vertex] = new_vertex_index;
                elements.push_back(new_vertex_index);
                vertices.push_back(vertex);
            } else {
                // if yes, just add its index in the elements vector
                elements.push_back(it->second);
            }
        }
    }

    return new our::Mesh(vertices, elements);
}

std::vector<our::mesh_utils::OBJSubMesh> our::mesh_utils::loadOBJWithMaterials(const std::string& filename){

    // The data loaded by Tiny OBJ Loader
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string base_dir;
    {
        std::filesystem::path p(filename);
        auto parent = p.parent_path();
        base_dir = parent.empty() ? std::string("./") : (parent.string() + std::string("/"));
    }

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), base_dir.c_str(), true)) {
        std::cerr << "Failed to load obj file \"" << filename << "\" due to error: " << err << std::endl;
        return {};
    }
    if (!warn.empty()) {
        std::cout << "WARN while loading obj file \"" << filename << "\": " << warn << std::endl;
    }

    // Group geometry by (object/group name, material id).
    // tinyobj uses -1 when no material is assigned.
    // We map "<objectName>#<materialId>" -> build buffers.
    std::unordered_map<std::string, SubMeshEntry> builds;

    for(const auto& shape : shapes){
        size_t index_offset = 0;
        const size_t face_count = shape.mesh.num_face_vertices.size();
        for(size_t f = 0; f < face_count; f++){
            int fv = shape.mesh.num_face_vertices[f];
            int mat_id = -1;
            if(f < shape.mesh.material_ids.size()) mat_id = shape.mesh.material_ids[f];

            const std::string objectName = shape.name.empty() ? std::string("__unnamed") : shape.name;
            const std::string key = objectName + std::string("#") + std::to_string(mat_id);
            auto& entry = builds[key];
            if(entry.objectName.empty()) entry.objectName = objectName;
            entry.materialId = mat_id;
            auto& build = entry.build;

            for(int v = 0; v < fv; v++){
                const tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                our::Vertex vertex = {};

                // Position
                if(idx.vertex_index >= 0 && (3 * idx.vertex_index + 2) < (int)attrib.vertices.size()){
                    vertex.position = {
                        attrib.vertices[3 * idx.vertex_index + 0],
                        attrib.vertices[3 * idx.vertex_index + 1],
                        attrib.vertices[3 * idx.vertex_index + 2]
                    };
                } else {
                    vertex.position = {0, 0, 0};
                }

                // Normal
                if(idx.normal_index >= 0 && (3 * idx.normal_index + 2) < (int)attrib.normals.size()){
                    vertex.normal = {
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    };
                } else {
                    vertex.normal = {0, 0, 1};
                }

                // TexCoord
                if(idx.texcoord_index >= 0 && (2 * idx.texcoord_index + 1) < (int)attrib.texcoords.size()){
                    vertex.tex_coord = {
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        attrib.texcoords[2 * idx.texcoord_index + 1]
                    };
                } else {
                    vertex.tex_coord = {0, 0};
                }

                // Color (if present in the OBJ)
                if(idx.vertex_index >= 0 && (3 * idx.vertex_index + 2) < (int)attrib.colors.size()){
                    float r = attrib.colors[3 * idx.vertex_index + 0] * 255.0f;
                    float g = attrib.colors[3 * idx.vertex_index + 1] * 255.0f;
                    float b = attrib.colors[3 * idx.vertex_index + 2] * 255.0f;
                    vertex.color = {
                        (glm::uint8)std::clamp(r, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(g, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(b, 0.0f, 255.0f),
                        255
                    };
                } else {
                    vertex.color = {255, 255, 255, 255};
                }

                // Deduplicate vertex per-submesh
                auto it = build.vertex_map.find(vertex);
                if(it == build.vertex_map.end()){
                    auto new_vertex_index = static_cast<GLuint>(build.vertices.size());
                    build.vertex_map[vertex] = new_vertex_index;
                    build.elements.push_back(new_vertex_index);
                    build.vertices.push_back(vertex);
                } else {
                    build.elements.push_back(it->second);
                }
            }

            index_offset += fv;
        }
    }

    std::vector<OBJSubMesh> result;
    result.reserve(builds.size());

    for(auto& kv : builds){
        auto& entry = kv.second;
        auto& build = entry.build;
        const int mat_id = entry.materialId;
        if(build.elements.empty() || build.vertices.empty()) continue;

        // Compute AABB & pivot, then re-center vertex positions around pivot.
        glm::vec3 minP(std::numeric_limits<float>::infinity());
        glm::vec3 maxP(-std::numeric_limits<float>::infinity());
        for(const auto& v : build.vertices){
            minP = glm::min(minP, v.position);
            maxP = glm::max(maxP, v.position);
        }
        const glm::vec3 pivot = 0.5f * (minP + maxP);
        const glm::vec3 aabbSize = (maxP - minP);
        for(auto& v : build.vertices){
            v.position -= pivot;
        }

        OBJSubMesh sub;
        sub.objectName = entry.objectName;
        sub.pivot = pivot;
        sub.aabbSize = aabbSize;
        sub.mesh = new our::Mesh(build.vertices, build.elements);

        if(mat_id >= 0 && mat_id < (int)materials.size()){
            const auto& mat = materials[mat_id];
            sub.materialName = mat.name;
            sub.diffuseColor = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
            if(!mat.diffuse_texname.empty()){
                // Resolve relative to base_dir.
                sub.diffuseTexturePath = base_dir + mat.diffuse_texname;

                // Some exporters omit Kd when a texture is present, leaving tinyobj's diffuse color at (0,0,0).
                // Our "textured" shader multiplies by tint, so a black Kd would black-out the texture.
                // Default to white tint when a diffuse texture exists and Kd appears unset.
                if(sub.diffuseColor == glm::vec3(0.0f)) sub.diffuseColor = glm::vec3(1.0f);
            }
        } else {
            sub.materialName = "__default";
            sub.diffuseColor = glm::vec3(1.0f);
        }
        result.push_back(std::move(sub));
    }

    return result;
}

// Create a sphere (the vertex order in the triangles are CCW from the outside)
// Segments define the number of divisions on the both the latitude and the longitude
our::Mesh* our::mesh_utils::sphere(const glm::ivec2& segments){
    std::vector<our::Vertex> vertices;
    std::vector<GLuint> elements;

    // We populate the sphere vertices by looping over its longitude and latitude
    for(int lat = 0; lat <= segments.y; lat++){
        float v = (float)lat / segments.y;
        float pitch = v * glm::pi<float>() - glm::half_pi<float>();
        float cos = glm::cos(pitch), sin = glm::sin(pitch);
        for(int lng = 0; lng <= segments.x; lng++){
            float u = (float)lng/segments.x;
            float yaw = u * glm::two_pi<float>();
            glm::vec3 normal = {cos * glm::cos(yaw), sin, cos * glm::sin(yaw)};
            glm::vec3 position = normal;
            glm::vec2 tex_coords = glm::vec2(u, v);
            our::Color color = our::Color(255, 255, 255, 255);
            vertices.push_back({position, color, tex_coords, normal});
        }
    }

    for(int lat = 1; lat <= segments.y; lat++){
        int start = lat*(segments.x+1);
        for(int lng = 1; lng <= segments.x; lng++){
            int prev_lng = lng-1;
            elements.push_back(lng + start);
            elements.push_back(lng + start - segments.x - 1);
            elements.push_back(prev_lng + start - segments.x - 1);
            elements.push_back(prev_lng + start - segments.x - 1);
            elements.push_back(prev_lng + start);
            elements.push_back(lng + start);
        }
    }

    return new our::Mesh(vertices, elements);
}