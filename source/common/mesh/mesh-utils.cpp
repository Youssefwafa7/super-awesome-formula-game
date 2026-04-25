#include "mesh-utils.hpp"
#include "../texture/texture-utils.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <iostream>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <glm/gtc/type_ptr.hpp>
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

    static std::string toLower(const std::string& s){
        std::string out;
        out.reserve(s.size());
        for(unsigned char ch : s) out.push_back((char)std::tolower(ch));
        return out;
    }

    static bool containsAnySubstring(const std::string& haystack, const std::vector<std::string>& needles){
        std::string h = toLower(haystack);
        for(const auto& n : needles){
            if(h.find(n) != std::string::npos) return true;
        }
        return false;
    }
}

our::Mesh* our::mesh_utils::loadModel(const std::string& filename) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filename,
        aiProcess_Triangulate |
        aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes |
        aiProcess_ImproveCacheLocality |
        aiProcess_LimitBoneWeights |
        aiProcess_PopulateArmatureData |
        aiProcess_GlobalScale |
        aiProcess_GenUVCoords |
        aiProcess_TransformUVCoords |
        aiProcess_SortByPType);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Failed to load model file \"" << filename << "\" due to error: " << importer.GetErrorString() << std::endl;
        return nullptr;
    }

    std::vector<our::Vertex> vertices;
    std::vector<GLuint> elements;
    std::unordered_map<our::Vertex, GLuint> vertex_map;

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];

        for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
            aiFace face = mesh->mFaces[j];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                unsigned int idx = face.mIndices[k];
                our::Vertex vertex = {};

                vertex.position = {
                    mesh->mVertices[idx].x,
                    mesh->mVertices[idx].y,
                    mesh->mVertices[idx].z
                };

                if (mesh->HasNormals()) {
                    vertex.normal = {
                        mesh->mNormals[idx].x,
                        mesh->mNormals[idx].y,
                        mesh->mNormals[idx].z
                    };
                } else {
                    vertex.normal = { 0, 0, 1 };
                }

                if (mesh->HasTextureCoords(0)) {
                    vertex.tex_coord = {
                        mesh->mTextureCoords[0][idx].x,
                        mesh->mTextureCoords[0][idx].y
                    };
                } else {
                    vertex.tex_coord = { 0, 0 };
                }

                if (mesh->HasVertexColors(0)) {
                    auto& color = mesh->mColors[0][idx];
                    vertex.color = {
                        (glm::uint8)std::clamp(color.r * 255.0f, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(color.g * 255.0f, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(color.b * 255.0f, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(color.a * 255.0f, 0.0f, 255.0f)
                    };
                } else {
                    vertex.color = { 255, 255, 255, 255 };
                }

                auto it = vertex_map.find(vertex);
                if (it == vertex_map.end()) {
                    auto new_vertex_index = static_cast<GLuint>(vertices.size());
                    vertex_map[vertex] = new_vertex_index;
                    elements.push_back(new_vertex_index);
                    vertices.push_back(vertex);
                } else {
                    elements.push_back(it->second);
                }
            }
        }
    }

    return new our::Mesh(vertices, elements);
}

static void ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform, std::unordered_map<std::string, SubMeshEntry>& builds, bool mergeByMaterial) {
    // Note: GLM is column-major, Assimp is row-major, so we transpose
    glm::mat4 nodeTransform = glm::transpose(glm::make_mat4(&node->mTransformation.a1));

    glm::mat4 globalTransform = parentTransform * nodeTransform;

    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        int mat_id = mesh->mMaterialIndex;
        
        // If mergeByMaterial is true, we group only by material ID.
        // Otherwise, we group by node name and material ID to preserve parts (e.g. for car wheels).
        const std::string key = mergeByMaterial ? std::to_string(mat_id) : (std::string(node->mName.C_Str()) + "##" + std::to_string(mat_id));
        
        auto& entry = builds[key];
        if (entry.objectName.empty()) entry.objectName = node->mName.C_Str();
        entry.materialId = mat_id;
        auto& build = entry.build;

        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(globalTransform)));

        for (unsigned int f = 0; f < mesh->mNumFaces; f++) {
            aiFace face = mesh->mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; k++) {
                unsigned int idx = face.mIndices[k];
                our::Vertex vertex = {};

                glm::vec4 pos(mesh->mVertices[idx].x, mesh->mVertices[idx].y, mesh->mVertices[idx].z, 1.0f);
                pos = globalTransform * pos;
                vertex.position = glm::vec3(pos);

                if (mesh->HasNormals()) {
                    glm::vec3 norm(mesh->mNormals[idx].x, mesh->mNormals[idx].y, mesh->mNormals[idx].z);
                    vertex.normal = glm::normalize(normalMatrix * norm);
                } else {
                    vertex.normal = { 0, 0, 1 };
                }

                if (mesh->HasTextureCoords(0)) {
                    vertex.tex_coord = {
                        mesh->mTextureCoords[0][idx].x,
                        mesh->mTextureCoords[0][idx].y
                    };
                } else {
                    vertex.tex_coord = { 0, 0 };
                }

                if (mesh->HasVertexColors(0)) {
                    auto& color = mesh->mColors[0][idx];
                    vertex.color = {
                        (glm::uint8)std::clamp(color.r * 255.0f, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(color.g * 255.0f, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(color.b * 255.0f, 0.0f, 255.0f),
                        (glm::uint8)std::clamp(color.a * 255.0f, 0.0f, 255.0f)
                    };
                } else {
                    vertex.color = { 255, 255, 255, 255 };
                }

                auto it = build.vertex_map.find(vertex);
                if (it == build.vertex_map.end()) {
                    auto new_vertex_index = static_cast<GLuint>(build.vertices.size());
                    build.vertex_map[vertex] = new_vertex_index;
                    build.elements.push_back(new_vertex_index);
                    build.vertices.push_back(vertex);
                } else {
                    build.elements.push_back(it->second);
                }
            }
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene, globalTransform, builds, mergeByMaterial);
    }
}

std::vector<our::mesh_utils::ModelSubMesh> our::mesh_utils::loadModelWithMaterials(const std::string& filename, bool mergeByMaterial) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filename,
        aiProcess_Triangulate |
        aiProcess_CalcTangentSpace |
        aiProcess_GenSmoothNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_OptimizeMeshes |
        aiProcess_ImproveCacheLocality |
        aiProcess_LimitBoneWeights |
        aiProcess_PopulateArmatureData |
        aiProcess_GlobalScale |
        aiProcess_GenUVCoords |
        aiProcess_TransformUVCoords |
        aiProcess_SortByPType);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cerr << "Failed to load model file \"" << filename << "\" due to error: " << importer.GetErrorString() << std::endl;
        return {};
    }

    std::string base_dir;
    {
        std::filesystem::path p(filename);
        auto parent = p.parent_path();
        base_dir = parent.empty() ? std::string("./") : (parent.string() + std::string("/"));
    }

    std::unordered_map<std::string, SubMeshEntry> builds;
    ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), builds, mergeByMaterial);

    std::vector<ModelSubMesh> result;
    result.reserve(builds.size());

    for (auto& kv : builds) {
        auto& entry = kv.second;
        auto& build = entry.build;
        const int mat_id = entry.materialId;
        if (build.elements.empty() || build.vertices.empty()) continue;

        glm::vec3 minP(std::numeric_limits<float>::infinity());
        glm::vec3 maxP(-std::numeric_limits<float>::infinity());
        for (const auto& v : build.vertices) {
            minP = glm::min(minP, v.position);
            maxP = glm::max(maxP, v.position);
        }
        const glm::vec3 pivot = 0.5f * (minP + maxP);
        const glm::vec3 aabbSize = (maxP - minP);
        for (auto& v : build.vertices) {
            v.position -= pivot;
        }

        ModelSubMesh sub;
        sub.objectName = entry.objectName;
        sub.pivot = pivot;
        sub.aabbSize = aabbSize;
        sub.mesh = new our::Mesh(build.vertices, build.elements);

        if (mat_id >= 0 && mat_id < (int)scene->mNumMaterials) {
            aiMaterial* mat = scene->mMaterials[mat_id];
            
            aiString name;
            mat->Get(AI_MATKEY_NAME, name);
            sub.materialName = name.C_Str();

            aiColor3D diffuseColor(1.f, 1.f, 1.f);
            if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) != AI_SUCCESS) {
                // If diffuse fails, try base color (common in GLTF/PBR)
                aiColor4D baseColor(1.f, 1.f, 1.f, 1.f);
                if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
                    diffuseColor = aiColor3D(baseColor.r, baseColor.g, baseColor.b);
                }
            }
            sub.diffuseColor = glm::vec3(diffuseColor.r, diffuseColor.g, diffuseColor.b);

            auto extractTex = [&](aiTextureType type, std::string& path, Texture2D*& tex) {
                if (mat->GetTextureCount(type) > 0) {
                    aiString texPath;
                    if (mat->GetTexture(type, 0, &texPath) == AI_SUCCESS) {
                        std::string p = texPath.C_Str();
                        if (p.empty()) return;
                        if (p[0] == '*') {
                            path = p;
                            int texIndex = std::stoi(p.substr(1));
                            if (texIndex >= 0 && texIndex < (int)scene->mNumTextures) {
                                aiTexture* aiTex = scene->mTextures[texIndex];
                                if (aiTex->mHeight == 0) {
                                    tex = texture_utils::loadImageFromMemory(reinterpret_cast<const unsigned char*>(aiTex->pcData), aiTex->mWidth);
                                }
                            }
                        } else {
                            path = base_dir + p;
                        }
                    }
                }
            };

            extractTex(aiTextureType_DIFFUSE, sub.diffuseTexturePath, sub.diffuseTexture);
            if (sub.diffuseTexturePath.empty()) extractTex(aiTextureType_BASE_COLOR, sub.diffuseTexturePath, sub.diffuseTexture);
            if (!sub.diffuseTexturePath.empty() && sub.diffuseColor == glm::vec3(0.0f)) sub.diffuseColor = glm::vec3(1.0f);

            // Requested logging format
            std::string modelName = std::filesystem::path(filename).stem().string();

            std::cerr << "model: " << modelName << ", material: " << sub.materialName << " has base color: (" 
                      << sub.diffuseColor.r << ", " << sub.diffuseColor.g << ", " << sub.diffuseColor.b << ", 1)\n";
            
            float metallicFactor = 0.0f;
            mat->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor);
            std::cerr << "model: " << modelName << ", material: " << sub.materialName << " has metallic factor: " << metallicFactor << "\n";

            float roughnessFactor = 1.0f;
            mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);
            std::cerr << "model: " << modelName << ", material: " << sub.materialName << " has roughness factor: " << roughnessFactor << "\n";

            aiColor3D emissiveColor(0.f, 0.f, 0.f);
            mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor);
            std::cerr << "model: " << modelName << ", material: " << sub.materialName << " has emissive color: (" 
                      << emissiveColor.r << ", " << emissiveColor.g << ", " << emissiveColor.b << ", 1)\n";

            auto formatTexPath = [&](const std::string& p) {
                if(p.empty()) return p;
                if(p[0] == '*') return modelName + "_" + p;
                return std::filesystem::path(p).filename().string();
            };

            auto logTex = [&](const std::string& texName, const std::string& path) {
                if (path.empty()) {
                    std::cerr << "model: " << modelName << ", material: " << sub.materialName << " has no " << texName << " texture\n";
                } else {
                    std::cerr << "model: " << modelName << ", material: " << sub.materialName << " has " << texName << " texture: " << formatTexPath(path) << "\n";
                }
            };

            logTex("albedo", sub.diffuseTexturePath);

            std::string normalPath; Texture2D* normalTex = nullptr;
            extractTex(aiTextureType_NORMALS, normalPath, normalTex);
            logTex("normal", normalPath);

            std::string unknownPath; Texture2D* unknownTex = nullptr;
            extractTex(aiTextureType_UNKNOWN, unknownPath, unknownTex);
            std::cerr << "model: " << modelName << ", material: " << sub.materialName << " has unknown texture type: " << (unknownPath.empty() ? "false" : "true") << "\n";

            extractTex(aiTextureType_SPECULAR, sub.specularTexturePath, sub.specularTexture);
            logTex("metallic", sub.specularTexturePath);

            extractTex(aiTextureType_DIFFUSE_ROUGHNESS, sub.roughnessTexturePath, sub.roughnessTexture);
            extractTex(aiTextureType_UNKNOWN, sub.roughnessTexturePath, sub.roughnessTexture);
            logTex("roughness", sub.roughnessTexturePath);

            extractTex(aiTextureType_AMBIENT, sub.aoTexturePath, sub.aoTexture);
            if (sub.aoTexturePath.empty()) extractTex(aiTextureType_LIGHTMAP, sub.aoTexturePath, sub.aoTexture);
            logTex("ambient occlusion", sub.aoTexturePath);

            extractTex(aiTextureType_EMISSIVE, sub.emissionTexturePath, sub.emissionTexture);
            if (sub.emissionTexturePath.empty()) extractTex(aiTextureType_EMISSION_COLOR, sub.emissionTexturePath, sub.emissionTexture);
            logTex("emissive", sub.emissionTexturePath);
        } else {
            sub.materialName = "__default";
            sub.diffuseColor = glm::vec3(1.0f);
        }
        result.push_back(std::move(sub));
    }

    return result;
}

our::Mesh* our::mesh_utils::sphere(const glm::ivec2& segments) {
    std::vector<our::Vertex> vertices;
    std::vector<GLuint> elements;

    for (int lat = 0; lat <= segments.y; lat++) {
        float v = (float)lat / segments.y;
        float pitch = v * glm::pi<float>() - glm::half_pi<float>();
        float cos = glm::cos(pitch), sin = glm::sin(pitch);
        for (int lng = 0; lng <= segments.x; lng++) {
            float u = (float)lng / segments.x;
            float yaw = u * glm::two_pi<float>();
            glm::vec3 normal = { cos * glm::cos(yaw), sin, cos * glm::sin(yaw) };
            glm::vec3 position = normal;
            glm::vec2 tex_coords = glm::vec2(u, v);
            our::Color color = our::Color(255, 255, 255, 255);
            vertices.push_back({ position, color, tex_coords, normal });
        }
    }

    for (int lat = 1; lat <= segments.y; lat++) {
        int start = lat * (segments.x + 1);
        for (int lng = 1; lng <= segments.x; lng++) {
            int prev_lng = lng - 1;
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