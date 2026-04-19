#pragma once

#include "../ecs/component.hpp"
#include "../mesh/mesh-utils.hpp"
#include "../mesh/mesh.hpp"
#include "../material/material.hpp"
#include "../texture/texture2d.hpp"
#include "../texture/texture-utils.hpp"
#include "../asset-loader.hpp"
#include "../ecs/transform.hpp"

#include <unordered_map>
#include <vector>
#include <string>

#include <glm/glm.hpp>

namespace our {

    // Renders an OBJ that uses multiple materials/textures (usemtl/map_Kd).
    // It loads and owns the generated meshes + textures + materials.
    class MultiMeshRendererComponent : public Component {
    public:
        struct Part {
            Mesh* mesh = nullptr;
            Material* material = nullptr;

            // Name metadata from OBJ/MTL for targeting specific parts (e.g., wheels).
            std::string objectName;
            std::string materialName;

            // Local transform applied on top of the owning entity's transform.
            // By default, we set position to the part pivot so rotations happen around the part center.
            Transform localTransform;

            // AABB size in the original OBJ local space (before pivot centering).
            glm::vec3 aabbSize = glm::vec3(0.0f);
        };

        std::vector<Part> parts;

        // Optional source path (useful for debug logs)
        std::string sourceObjPath;

        // Optional filtering configured via JSON.
        std::vector<std::string> excludeObjects;
        std::vector<std::string> excludeMaterials;

        bool debugPrintParts = false;

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
