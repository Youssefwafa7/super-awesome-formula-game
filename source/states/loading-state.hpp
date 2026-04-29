#pragma once

#include <application.hpp>
#include <asset-loader.hpp>
#include <imgui.h>
#include <texture/texture-utils.hpp>
#include <vector>
#include <string>

class Loadingstate : public our::State {
    float progress = 0.0f;
    bool loadingStarted = false;
    int frameCounter = 0;
    our::Texture2D* bgTexture = nullptr;
    
    // Progressive loading: track asset categories to load
    std::vector<std::pair<std::string, std::string>> assetQueue; // {category, key}
    size_t currentAssetIndex = 0;
    bool assetsParsed = false;

    void parseAssetCategories(const nlohmann::json& assets) {
        if (!assets.is_object()) return;
        
        // Define load order: shaders first (fast), then textures/meshes (slower)
        const char* order[] = {"samplers", "shaders", "materials", "textures", "meshes"};
        
        for (const char* category : order) {
            if (assets.contains(category) && assets[category].is_object()) {
                for (auto it = assets[category].begin(); it != assets[category].end(); ++it) {
                    assetQueue.push_back({category, it.key()});
                }
            }
        }
        
        assetsParsed = true;
        printf("Loading: Found %zu assets to load\n", assetQueue.size());
    }

    void loadNextAssetChunk(const nlohmann::json& assets) {
        if (!assetsParsed || currentAssetIndex >= assetQueue.size()) {
            progress = 1.0f;
            loadingStarted = false;
            return;
        }
        
        // Load a chunk of assets per frame (adjust chunk size for balance)
        const size_t chunkSize = 3;
        size_t end = std::min(currentAssetIndex + chunkSize, assetQueue.size());
        
        for (size_t i = currentAssetIndex; i < end; ++i) {
            const auto& [category, key] = assetQueue[i];
            
            if (category == "shaders" && assets.contains("shaders")) {
                auto& shaderData = assets["shaders"];
                if (shaderData.contains(key)) {
                    our::AssetLoader<our::ShaderProgram>::deserialize({{key, shaderData[key]}});
                }
            } else if (category == "textures" && assets.contains("textures")) {
                auto& texData = assets["textures"];
                if (texData.contains(key)) {
                    our::AssetLoader<our::Texture2D>::deserialize({{key, texData[key]}});
                }
            } else if (category == "meshes" && assets.contains("meshes")) {
                auto& meshData = assets["meshes"];
                if (meshData.contains(key)) {
                    our::AssetLoader<our::Mesh>::deserialize({{key, meshData[key]}});
                }
            } else if (category == "materials" && assets.contains("materials")) {
                auto& matData = assets["materials"];
                if (matData.contains(key)) {
                    our::AssetLoader<our::Material>::deserialize({{key, matData[key]}});
                }
            } else if (category == "samplers" && assets.contains("samplers")) {
                auto& sampData = assets["samplers"];
                if (sampData.contains(key)) {
                    our::AssetLoader<our::Sampler>::deserialize({{key, sampData[key]}});
                }
            }
        }
        
        currentAssetIndex = end;
        progress = static_cast<float>(currentAssetIndex) / static_cast<float>(assetQueue.size());
        
        // Log progress every few assets
        if (currentAssetIndex % 10 == 0 || currentAssetIndex == assetQueue.size()) {
            printf("Loading: %.0f%% (%zu/%zu)\n", progress * 100, currentAssetIndex, assetQueue.size());
        }
    }

    void onInitialize() override {
        progress = 0.0f;
        loadingStarted = true;
        frameCounter = 0;
        currentAssetIndex = 0;
        assetsParsed = false;
        assetQueue.clear();
        bgTexture = our::texture_utils::loadImage("assets/textures/background.png");
    }

    void onDraw(double deltaTime) override {
        glClearColor(0.04f, 0.04f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        frameCounter++;

        // First frame: parse assets to get the queue
        if (loadingStarted && frameCounter == 1) {
            auto& fullConfig = getApp()->getConfig();
            if (fullConfig.contains("scene") && fullConfig["scene"].contains("assets")) {
                parseAssetCategories(fullConfig["scene"]["assets"]);
            } else {
                printf("ERROR: No assets found in config!\n");
                progress = 1.0f;
                loadingStarted = false;
            }
        }

        // Subsequent frames: load assets progressively
        if (loadingStarted && frameCounter > 1 && currentAssetIndex < assetQueue.size()) {
            auto& fullConfig = getApp()->getConfig();
            if (fullConfig.contains("scene") && fullConfig["scene"].contains("assets")) {
                loadNextAssetChunk(fullConfig["scene"]["assets"]);
            }
        }

        // Check if loading is complete
        if (loadingStarted && currentAssetIndex >= assetQueue.size()) {
            printf("Assets Loaded Successfully\n");
            loadingStarted = false;
        }

        // Transition to play state when done
        if (!loadingStarted && progress >= 1.0f) {
            getApp()->changeState("play");
        }
    }

    void onDestroy() override {
        if (bgTexture) {
            delete bgTexture;
            bgTexture = nullptr;
        }
    }

    void onImmediateGui() override {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(display);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;

        if (bgTexture) {
            ImGui::GetBackgroundDrawList()->AddImage(
                (ImTextureID)(intptr_t)bgTexture->getOpenGLName(),
                ImVec2(0, 0),
                display,
                ImVec2(0, 1),
                ImVec2(1, 0)
            );
        }

        ImGui::Begin("LoadingScreen", nullptr, flags);
        
        // Centering logic
        float barWidth = display.x * 0.5f;
        ImGui::SetCursorPos(ImVec2((display.x - barWidth) * 0.5f, display.y * 0.5f));
        
        // F1 Style Progress Bar
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.91f, 0.00f, 0.18f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.00f));
        ImGui::ProgressBar(progress, ImVec2(barWidth, 20.0f), "");
        ImGui::PopStyleColor(2);

        const char* text = "INITIALIZING TRACK ASSETS...";
        float textW = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPos(ImVec2((display.x - textW) * 0.5f, display.y * 0.5f - 30.0f));
        ImGui::Text("%s", text);

        ImGui::End();
    }
};