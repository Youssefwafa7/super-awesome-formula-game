#pragma once

#include "../ecs/world.hpp"
#include "../components/camera.hpp"
#include "../components/mesh-renderer.hpp"
#include "../components/light.hpp"
#include "../asset-loader.hpp"

#include <glad/gl.h>
#include <vector>
#include <algorithm>

namespace our
{
    
    // The render command stores command that tells the renderer that it should draw
    // the given mesh at the given localToWorld matrix using the given material
    // The renderer will fill this struct using the mesh renderer components
    struct RenderCommand {
        glm::mat4 localToWorld;
        glm::vec3 center;
        Mesh* mesh;
        Material* material;
    };

    // A forward renderer is a renderer that draw the object final color directly to the framebuffer
    // In other words, the fragment shader in the material should output the color that we should see on the screen
    // This is different from more complex renderers that could draw intermediate data to a framebuffer before computing the final color
    // In this project, we only need to implement a forward renderer
    class ForwardRenderer {
        static constexpr int MAX_LIGHTS = 16;

        // These window size will be used on multiple occasions (setting the viewport, computing the aspect ratio, etc.)
        glm::ivec2 windowSize;
        // These are two vectors in which we will store the opaque and the transparent commands.
        // We define them here (instead of being local to the "render" function) as an optimization to prevent reallocating them every frame
        std::vector<RenderCommand> opaqueCommands;
        std::vector<RenderCommand> transparentCommands;
        // Objects used for rendering a skybox
        Mesh* skySphere;
        TexturedMaterial* skyMaterial;
        // Objects used for rendering a sun sphere (in front of sky, behind everything else)
        Mesh* sunSphere;
        TexturedMaterial* sunMaterial;

        // Sun placement parameters (world anchored)
        // Sun direction is the direction of light rays (from sun toward the scene).
        // The visual sun sprite is rendered in the opposite direction.
        glm::vec3 sunDirectionWorld = glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f));
        float sunDistance = 800.0f; // distance from camera to place the visual sun (prevents far clip)
        float sunScale = 40.0f;

        // Optional renderer-driven sun light (directional)
        bool enableSunLight = false;
        glm::vec3 sunLightColor = glm::vec3(1.0f, 0.97f, 0.9f);
        float sunLightIntensity = 1.6f;
        // Objects used for Postprocessing
        GLuint postprocessFrameBuffer, postProcessVertexArray;
        Texture2D *colorTarget, *depthTarget;
        TexturedMaterial* postprocessMaterial;

        // Global ambient (set from renderer config)
        glm::vec3 ambientColor = glm::vec3(0.02f, 0.02f, 0.03f);
        float ambientIntensity = 1.0f;
    public:
        // Initialize the renderer including the sky and the Postprocessing objects.
        // windowSize is the width & height of the window (in pixels).
        void initialize(glm::ivec2 windowSize, const nlohmann::json& config);
        // Clean up the renderer
        void destroy();
        // This function should be called every frame to draw the given world
        void render(World* world);


    };

}