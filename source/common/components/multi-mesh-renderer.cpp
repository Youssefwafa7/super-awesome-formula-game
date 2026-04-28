#include "multi-mesh-renderer.hpp"

#include "../deserialize-utils.hpp"
#include "../ecs/entity.hpp"

#include <glad/gl.h>
#include <glm/vec4.hpp>
#include <glm/vec3.hpp>

#include <iostream>
#include <algorithm>

namespace our {

    static bool containsSubstring(const std::vector<std::string>& list, const std::string& value){
        for (const auto& item : list) {
            if (value.find(item) != std::string::npos) return true;
        }
        return false;
    }

    Texture2D* MultiMeshRendererComponent::getFallbackWhiteTexture(){
        if(fallbackWhiteTexture) return fallbackWhiteTexture;

        // Create a 1x1 white RGBA texture to avoid sampling from an unbound texture.
        fallbackWhiteTexture = new Texture2D();
        ownedTextures.push_back(fallbackWhiteTexture);

        glBindTexture(GL_TEXTURE_2D, fallbackWhiteTexture->getOpenGLName());
        const unsigned char white[] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

        // Ensure the texture is always safe to sample even if the sampler uses mipmapped filtering.
        glGenerateMipmap(GL_TEXTURE_2D);

        return fallbackWhiteTexture;
    }

    Texture2D* MultiMeshRendererComponent::getOrLoadTexture(const std::string& path){
        if(path.empty()) return getFallbackWhiteTexture();

        if(auto it = textureCache.find(path); it != textureCache.end()) return it->second;

        Texture2D* tex = texture_utils::loadImage(path);
        if(tex == nullptr) tex = getFallbackWhiteTexture();
        else ownedTextures.push_back(tex);

        textureCache[path] = tex;
        return tex;
    }

    void MultiMeshRendererComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        std::string modelPath = data.value("model", "");
        if(modelPath.empty()){
            modelPath = data.value("obj", "");
        }
        if(modelPath.empty()){
            const std::string meshAssetName = data.value("mesh", "");
            if(meshAssetName.empty()){
                std::cerr << "[MultiMeshRendererComponent] Missing 'model' or 'obj' path (or 'mesh' asset name)" << std::endl;
                return;
            }

            const std::string* resolved = getMeshAssetPath(meshAssetName);
            if(resolved == nullptr || resolved->empty()){
                std::cerr << "[MultiMeshRendererComponent] Unknown mesh asset (no source path): \"" << meshAssetName << "\"" << std::endl;
                return;
            }

            modelPath = *resolved;
        }

        sourceObjPath = modelPath;

        excludeObjects.clear();
        excludeMaterials.clear();
        debugPrintParts = data.value("debugPrintParts", false);

        const bool recenterToOrigin = data.value("recenterToOrigin", false);
        mergeByMaterial = data.value("mergeByMaterial", false);
        preserveHierarchy = data.value("preserveHierarchy", false);

        if(data.contains("excludeObjects") && data["excludeObjects"].is_array()){
            excludeObjects = data["excludeObjects"].get<std::vector<std::string>>();
        }
        if(data.contains("excludeMaterials") && data["excludeMaterials"].is_array()){
            excludeMaterials = data["excludeMaterials"].get<std::vector<std::string>>();
        }

        const std::string shaderName = data.value("shader", "textured");
        const std::string samplerName = data.value("sampler", "default");

        ShaderProgram* shader = AssetLoader<ShaderProgram>::get(shaderName);
        if(shader == nullptr){
            std::cerr << "[MultiMeshRendererComponent] Missing shader asset: \"" << shaderName << "\"" << std::endl;
            return;
        }

        Sampler* sampler = AssetLoader<Sampler>::get(samplerName);
        if(sampler == nullptr){
            std::cerr << "[MultiMeshRendererComponent] Missing sampler asset: \"" << samplerName << "\"" << std::endl;
        }

        PipelineState pipelineState;
        if(data.contains("pipelineState")){
            pipelineState.deserialize(data["pipelineState"]);
        }

        glm::vec4 globalTint(1.0f);
        if(data.contains("tint")){
            globalTint = data["tint"].get<glm::vec4>();
        }

        // Build sub-meshes split by material.
        auto modelData = mesh_utils::loadModelWithMaterials(modelPath, mergeByMaterial, preserveHierarchy);
        auto& submeshes = modelData.submeshes;
        if(submeshes.empty()){
            std::cerr << "[MultiMeshRendererComponent] No submeshes loaded from: " << modelPath << std::endl;
            return;
        }

        if(debugPrintParts){
            const std::string entityName = (getOwner() ? getOwner()->name : std::string("<null>"));
            std::cerr << "[MultiMeshRendererComponent] Submeshes for entity \"" << entityName << "\" from: " << modelPath << std::endl;
            for(size_t i = 0; i < submeshes.size(); i++){
                const auto& s = submeshes[i];
                const bool excludedByObject = (!excludeObjects.empty() && containsSubstring(excludeObjects, s.objectName));
                const bool excludedByMaterial = (!excludeMaterials.empty() && containsSubstring(excludeMaterials, s.materialName));
                std::cerr << "  [" << i << "] object=\"" << s.objectName << "\" material=\"" << s.materialName
                          << "\" tex=\"" << s.diffuseTexturePath << "\" pivot=(" << s.pivot.x << "," << s.pivot.y << "," << s.pivot.z << ")"
                          << " size=(" << s.aabbSize.x << "," << s.aabbSize.y << "," << s.aabbSize.z << ")"
                          << (excludedByObject || excludedByMaterial ? "  [EXCLUDED]" : "")
                          << std::endl;
            }
        }

        parts.clear();
        parts.reserve(submeshes.size());

        for(auto& sub : submeshes){
            if(sub.mesh == nullptr) continue;

            const bool excludedByObject = (!excludeObjects.empty() && containsSubstring(excludeObjects, sub.objectName));
            const bool excludedByMaterial = (!excludeMaterials.empty() && containsSubstring(excludeMaterials, sub.materialName));
            if(excludedByObject || excludedByMaterial){
                delete sub.mesh;
                continue;
            }

            Material* mat = nullptr;
            if(shaderName == "lit"){
                auto* lit = new LitMaterial();
                lit->shader = shader;
                lit->sampler = sampler;
                lit->pipelineState = pipelineState;
                lit->transparent = false;

                // Preserve classic OBJ/MTL meaning:
                // - diffuseColor modulates the diffuse texture
                // - JSON/global tint acts as an extra multiplier
                lit->tint = globalTint;
                lit->albedoColor = sub.diffuseColor;
                
                auto assignTexture = [&](Texture2D*& outMap, Texture2D* loadedTex, const std::string& path) {
                    if (loadedTex) {
                        outMap = loadedTex;
                        if (std::find(ownedTextures.begin(), ownedTextures.end(), loadedTex) == ownedTextures.end()) {
                            ownedTextures.push_back(loadedTex);
                        }
                    } else if (!path.empty()) {
                        outMap = getOrLoadTexture(path);
                    }
                };

                assignTexture(lit->albedoMap, sub.diffuseTexture, sub.diffuseTexturePath);
                assignTexture(lit->specularMap, sub.specularTexture, sub.specularTexturePath);
                assignTexture(lit->roughnessMap, sub.roughnessTexture, sub.roughnessTexturePath);
                assignTexture(lit->aoMap, sub.aoTexture, sub.aoTexturePath);
                assignTexture(lit->emissionMap, sub.emissionTexture, sub.emissionTexturePath);

                mat = lit;
            } else {
                auto* tex = new TexturedMaterial();
                tex->shader = shader;
                tex->sampler = sampler;
                tex->pipelineState = pipelineState;
                tex->transparent = false;
                tex->alphaThreshold = 0.0f;

                // Diffuse color from MTL * global tint.
                glm::vec4 diffuseTint(sub.diffuseColor, 1.0f);
                tex->tint = diffuseTint * globalTint;

                if (sub.diffuseTexture) {
                    tex->texture = sub.diffuseTexture;
                    if (std::find(ownedTextures.begin(), ownedTextures.end(), sub.diffuseTexture) == ownedTextures.end()) {
                        ownedTextures.push_back(sub.diffuseTexture);
                    }
                } else {
                    tex->texture = getOrLoadTexture(sub.diffuseTexturePath);
                }
                
                mat = tex;
            }

            ownedMeshes.push_back(sub.mesh);
            ownedMaterials.push_back(mat);

            Part part;
            part.mesh = sub.mesh;
            part.material = mat;
            part.objectName = sub.objectName;
            part.materialName = sub.materialName;
            part.localTransform.position = sub.pivot;
            part.localTransform.rotation = glm::vec3(0.0f);
            part.localTransform.scale = glm::vec3(1.0f);
            part.aabbSize = sub.aabbSize;
            parts.push_back(std::move(part));

            if(debugPrintParts){
                std::cerr << "  [" << (parts.size() - 1) << "] object=\"" << sub.objectName << "\" material=\"" << sub.materialName
                          << "\" tex=\"" << sub.diffuseTexturePath << "\""
                          << " pivot=(" << sub.pivot.x << "," << sub.pivot.y << "," << sub.pivot.z << ")"
                          << " size=(" << sub.aabbSize.x << "," << sub.aabbSize.y << "," << sub.aabbSize.z << ")" << std::endl;
            }
        }

        // Some OBJs are authored far away from the origin (all coordinates positive, etc.).
        // Since the chase camera and gameplay systems operate on the entity origin,
        // optionally re-center the whole multi-mesh around its overall AABB center.
        if(recenterToOrigin && !parts.empty()){
            glm::vec3 minP(std::numeric_limits<float>::infinity());
            glm::vec3 maxP(-std::numeric_limits<float>::infinity());

            for(const auto& part : parts){
                const glm::vec3 half = 0.5f * glm::abs(part.aabbSize);
                minP = glm::min(minP, part.localTransform.position - half);
                maxP = glm::max(maxP, part.localTransform.position + half);
            }

            const glm::vec3 pivot = 0.5f * (minP + maxP);
            for(auto& part : parts){
                part.localTransform.position -= pivot;
            }

            if(debugPrintParts){
                std::cerr << "[MultiMeshRendererComponent] Recentering enabled. Overall pivot=(" << pivot.x << "," << pivot.y << "," << pivot.z << ")" << std::endl;
            }
        }

        // Build the component node hierarchy from ModelData
        if(preserveHierarchy && modelData.rootNode){
            std::function<Node*(our::mesh_utils::ModelNode*, Node*)> convertNode = [&](our::mesh_utils::ModelNode* src, Node* parent) {
                Node* dst = new Node();
                dst->name = src->name;
                dst->localTransform.position = src->position;
                dst->localTransform.rotation = src->rotation;
                dst->localTransform.scale = src->scale;
                dst->originalTransform = dst->localTransform;
                dst->parent = parent;
                dst->partIndices = src->meshIndices;
                
                for(auto* childSrc : src->children){
                    dst->children.push_back(convertNode(childSrc, dst));
                }
                return dst;
            };
            rootNode = convertNode(modelData.rootNode, nullptr);
            
            if(debugPrintParts){
                std::function<void(Node*, int)> printNode = [&](Node* n, int depth){
                    std::string indent(depth * 2, ' ');
                    std::cerr << indent << "- Node: " << n->name << " (parts: " << n->partIndices.size() << ")" << std::endl;
                    for(auto c : n->children) printNode(c, depth + 1);
                };
                std::cerr << "[MultiMeshRendererComponent] Scene Graph:" << std::endl;
                printNode(rootNode, 0);
            }
        }
    }

    MultiMeshRendererComponent::~MultiMeshRendererComponent(){
        delete rootNode;
        // Note: shader/sampler are typically owned by AssetLoader.
        for(auto* m : ownedMaterials) delete m;
        for(auto* mesh : ownedMeshes) delete mesh;
        for(auto* tex : ownedTextures) delete tex;
    }

}
