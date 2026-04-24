#include "material.hpp"

#include "../asset-loader.hpp"
#include "deserialize-utils.hpp"

#include <iostream>

namespace our {

    // This function should setup the pipeline state and set the shader to be used
    void Material::setup() const {
        //TODO: (Req 7) Write this function
        pipelineState.setup();
        if(!shader){
            std::cerr << "ERROR: Material has no shader (nullptr)" << std::endl;
            return;
        }
        shader->use();
    }

    // This function read the material data from a json object
    void Material::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        if(data.contains("pipelineState")){
            pipelineState.deserialize(data["pipelineState"]);
        }
        shader = AssetLoader<ShaderProgram>::get(data["shader"].get<std::string>());
        transparent = data.value("transparent", false);
    }

    // This function should call the setup of its parent and
    // set the "tint" uniform to the value in the member variable tint 
    void TintedMaterial::setup() const {
        //TODO: (Req 7) Write this function
        Material::setup();
        if(shader) shader->set("tint", tint);
    }

    // This function read the material data from a json object
    void TintedMaterial::deserialize(const nlohmann::json& data){
        Material::deserialize(data);
        if(!data.is_object()) return;
        tint = data.value("tint", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // This function should call the setup of its parent and
    // set the "alphaThreshold" uniform to the value in the member variable alphaThreshold
    // Then it should bind the texture and sampler to a texture unit and send the unit number to the uniform variable "tex" 
    void TexturedMaterial::setup() const {
        //TODO: (Req 7) Write this function
        TintedMaterial::setup();
        if(shader) shader->set("alphaThreshold", alphaThreshold);
        glActiveTexture(GL_TEXTURE0);
        if(texture) texture->bind();
        else glBindTexture(GL_TEXTURE_2D, 0);

        if(sampler) sampler->bind(0);
        else glBindSampler(0, 0);

        if(shader) shader->set("tex", 0);
    }

    // This function read the material data from a json object
    void TexturedMaterial::deserialize(const nlohmann::json& data){
        TintedMaterial::deserialize(data);
        if(!data.is_object()) return;
        alphaThreshold = data.value("alphaThreshold", 0.0f);
        texture = AssetLoader<Texture2D>::get(data.value("texture", ""));
        sampler = AssetLoader<Sampler>::get(data.value("sampler", ""));
    }

    static void bindTextureUnit(GLuint unit, Texture2D* texture, Sampler* sampler){
        glActiveTexture(GL_TEXTURE0 + unit);
        if(texture) texture->bind();
        else glBindTexture(GL_TEXTURE_2D, 0);

        if(sampler) sampler->bind(unit);
        else glBindSampler(unit, 0);
    }

    void LitMaterial::setup() const {
        // Note: We keep the same base behavior as TintedMaterial (pipeline + shader + tint).
        TintedMaterial::setup();

        if(shader == nullptr) return;

        shader->set("hasAlbedoMap", (GLint)(albedoMap != nullptr));
        shader->set("hasSpecularMap", (GLint)(specularMap != nullptr));
        shader->set("hasRoughnessMap", (GLint)(roughnessMap != nullptr));
        shader->set("hasAoMap", (GLint)(aoMap != nullptr));
        shader->set("hasEmissionMap", (GLint)(emissionMap != nullptr));

        shader->set("albedoColor", albedoColor);
        shader->set("specularColor", specularColor);
        shader->set("roughnessValue", roughnessValue);
        shader->set("metallicValue", metallicValue);
        shader->set("aoValue", aoValue);
        shader->set("emissionColor", emissionColor);
        shader->set("emissionIntensity", emissionIntensity);
        shader->set("alphaThreshold", alphaThreshold);
        shader->set("useBlinnPhong", (GLint)useBlinnPhong);

        // Bind maps to fixed texture units.
        // 0: albedo, 1: specular, 2: roughness, 3: AO, 4: emission
        bindTextureUnit(0, albedoMap, sampler);
        bindTextureUnit(1, specularMap, sampler);
        bindTextureUnit(2, roughnessMap, sampler);
        bindTextureUnit(3, aoMap, sampler);
        bindTextureUnit(4, emissionMap, sampler);

        shader->set("albedoMap", 0);
        shader->set("specularMap", 1);
        shader->set("roughnessMap", 2);
        shader->set("aoMap", 3);
        shader->set("emissionMap", 4);
    }

    void LitMaterial::deserialize(const nlohmann::json& data){
        TintedMaterial::deserialize(data);
        if(!data.is_object()) return;

        albedoMap = AssetLoader<Texture2D>::get(data.value("albedoMap", ""));
        specularMap = AssetLoader<Texture2D>::get(data.value("specularMap", ""));
        roughnessMap = AssetLoader<Texture2D>::get(data.value("roughnessMap", ""));
        aoMap = AssetLoader<Texture2D>::get(data.value("aoMap", ""));
        emissionMap = AssetLoader<Texture2D>::get(data.value("emissionMap", ""));

        sampler = AssetLoader<Sampler>::get(data.value("sampler", ""));

        albedoColor = data.value("albedoColor", albedoColor);
        specularColor = data.value("specularColor", specularColor);
        roughnessValue = data.value("roughnessValue", roughnessValue);
        metallicValue = data.value("metallicValue", metallicValue);
        aoValue = data.value("aoValue", aoValue);
        emissionColor = data.value("emissionColor", emissionColor);
        emissionIntensity = data.value("emissionIntensity", emissionIntensity);
        alphaThreshold = data.value("alphaThreshold", alphaThreshold);

        useBlinnPhong = data.value("useBlinnPhong", useBlinnPhong);
    }

}