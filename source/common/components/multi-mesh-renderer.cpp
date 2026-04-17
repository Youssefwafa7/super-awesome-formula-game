#include "multi-mesh-renderer.hpp"

#include "../deserialize-utils.hpp"

#include <glad/gl.h>
#include <glm/vec4.hpp>

#include <iostream>

namespace our {

    Texture2D* MultiMeshRendererComponent::getFallbackWhiteTexture(){
        if(fallbackWhiteTexture) return fallbackWhiteTexture;

        // Create a 1x1 white RGBA texture to avoid sampling from an unbound texture.
        fallbackWhiteTexture = new Texture2D();
        ownedTextures.push_back(fallbackWhiteTexture);

        glBindTexture(GL_TEXTURE_2D, fallbackWhiteTexture->getOpenGLName());
        const unsigned char white[] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

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

        std::string objPath = data.value("obj", "");
        if(objPath.empty()){
            const std::string meshAssetName = data.value("mesh", "");
            if(meshAssetName.empty()){
                std::cerr << "[MultiMeshRendererComponent] Missing 'obj' path (or 'mesh' asset name)" << std::endl;
                return;
            }

            const std::string* resolved = getMeshAssetPath(meshAssetName);
            if(resolved == nullptr || resolved->empty()){
                std::cerr << "[MultiMeshRendererComponent] Unknown mesh asset (no source path): \"" << meshAssetName << "\"" << std::endl;
                return;
            }

            objPath = *resolved;
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
        auto submeshes = mesh_utils::loadOBJWithMaterials(objPath);
        if(submeshes.empty()){
            std::cerr << "[MultiMeshRendererComponent] No submeshes loaded from: " << objPath << std::endl;
            return;
        }

        parts.clear();
        parts.reserve(submeshes.size());

        for(auto& sub : submeshes){
            if(sub.mesh == nullptr) continue;

            auto* mat = new TexturedMaterial();
            mat->shader = shader;
            mat->sampler = sampler;
            mat->pipelineState = pipelineState;
            mat->transparent = false;
            mat->alphaThreshold = 0.0f;

            // Diffuse color from MTL * global tint.
            glm::vec4 diffuseTint(sub.diffuseColor, 1.0f);
            mat->tint = diffuseTint * globalTint;

            mat->texture = getOrLoadTexture(sub.diffuseTexturePath);

            ownedMeshes.push_back(sub.mesh);
            ownedMaterials.push_back(mat);
            parts.push_back({sub.mesh, mat});
        }
    }

    MultiMeshRendererComponent::~MultiMeshRendererComponent(){
        // Note: shader/sampler are typically owned by AssetLoader.
        for(auto* m : ownedMaterials) delete m;
        for(auto* mesh : ownedMeshes) delete mesh;
        for(auto* tex : ownedTextures) delete tex;
    }

}
