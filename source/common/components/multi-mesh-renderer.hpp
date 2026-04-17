#pragma once

#include "../ecs/component.hpp"
#include "../mesh/mesh-utils.hpp"
#include "../mesh/mesh.hpp"
#include "../material/material.hpp"
#include "../texture/texture2d.hpp"
#include "../texture/texture-utils.hpp"
#include "../asset-loader.hpp"

#include <unordered_map>
#include <vector>
#include <string>

namespace our {

    // Renders an OBJ that uses multiple materials/textures (usemtl/map_Kd).
    // It loads and owns the generated meshes + textures + materials.
    class MultiMeshRendererComponent : public Component {
    public:
        struct Part {
            Mesh* mesh = nullptr;
            Material* material = nullptr;
        };

        std::vector<Part> parts;

        static std::string getID() { return "Multi Mesh Renderer"; }

        void deserialize(const nlohmann::json& data) override;

        ~MultiMeshRendererComponent() override;

    private:
        std::vector<Mesh*> ownedMeshes;
        std::vector<Material*> ownedMaterials;
        std::vector<Texture2D*> ownedTextures;

        // Texture cache by resolved filename.
        std::unordered_map<std::string, Texture2D*> textureCache;

        Texture2D* fallbackWhiteTexture = nullptr;

        Texture2D* getOrLoadTexture(const std::string& path);
        Texture2D* getFallbackWhiteTexture();
    };

}
