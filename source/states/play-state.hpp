#define IMGUI_DEFINE_MATH_OPERATORS
#pragma once

#include <application.hpp>

#include <ecs/world.hpp>
#include <systems/forward-renderer.hpp>
#include <systems/free-camera-controller.hpp>
#include <systems/movement.hpp>
#include <systems/car-controller.hpp>
#include <systems/chase-camera.hpp>
#include <systems/wheel-spin.hpp>
#include <asset-loader.hpp>

#include <string>

// This state shows how to use the ECS framework and deserialization.
class Playstate: public our::State {

    our::World world;
    our::ForwardRenderer renderer;
    our::FreeCameraControllerSystem cameraController;
    our::MovementSystem movementSystem;
    our::CarControllerSystem carControllerSystem;
    our::ChaseCameraSystem chaseCameraSystem;
    our::WheelSpinSystem wheelSpinSystem;

    static our::Entity* findEntityByName(our::World& world, const std::string& name){
        for(auto* e : world.getEntities()){
            if(e && e->name == name) return e;
        }
        return nullptr;
    }

    static const nlohmann::json* findPresetById(const nlohmann::json& arr, const std::string& id){
        if(!arr.is_array() || id.empty()) return nullptr;
        for(const auto& item : arr){
            if(item.is_object() && item.value("id", std::string{}) == id) return &item;
        }
        return nullptr;
    }

    static const nlohmann::json* firstPreset(const nlohmann::json& arr){
        if(!arr.is_array() || arr.empty()) return nullptr;
        if(arr[0].is_object()) return &arr[0];
        return nullptr;
    }

    static nlohmann::json buildWorldWithPresets(const nlohmann::json& sceneConfig, const std::string& carId, const std::string& trackId){
        nlohmann::json out = nlohmann::json::array();

        // Start from the configured world but remove any hardcoded player/track to avoid duplicates.
        if(sceneConfig.contains("world") && sceneConfig["world"].is_array()){
            for(const auto& e : sceneConfig["world"]){
                if(!e.is_object()) continue;
                const std::string name = e.value("name", std::string{});
                if(name == "player" || name == "track") continue;
                out.push_back(e);
            }
        }

        if(!sceneConfig.contains("presets")) return out;
        const auto& presets = sceneConfig["presets"];
        const auto& cars = presets.contains("cars") ? presets["cars"] : nlohmann::json::array();
        const auto& tracks = presets.contains("tracks") ? presets["tracks"] : nlohmann::json::array();

        const nlohmann::json* carPreset = findPresetById(cars, carId);
        if(carPreset == nullptr) carPreset = firstPreset(cars);
        const nlohmann::json* trackPreset = findPresetById(tracks, trackId);
        if(trackPreset == nullptr) trackPreset = firstPreset(tracks);

        if(trackPreset != nullptr){
            if(trackPreset->contains("entities") && (*trackPreset)["entities"].is_array()){
                for(const auto& ent : (*trackPreset)["entities"]){
                    if(ent.is_object()) out.push_back(ent);
                }
            } else if(trackPreset->contains("entity") && (*trackPreset)["entity"].is_object()){
                out.push_back((*trackPreset)["entity"]);
            }
        }

        if(carPreset != nullptr && carPreset->contains("entity") && (*carPreset)["entity"].is_object()){
            nlohmann::json player = (*carPreset)["entity"];

            // Override spawn from track preset if provided.
            if(trackPreset != nullptr && trackPreset->contains("spawn") && (*trackPreset)["spawn"].is_object()){
                const auto& spawn = (*trackPreset)["spawn"];
                if(spawn.contains("position")) player["position"] = spawn["position"];
                if(spawn.contains("rotation")) player["rotation"] = spawn["rotation"];
            }

            out.push_back(std::move(player));
        }

        return out;
    }

    void onInitialize() override {
        // First of all, we get the scene configuration from the app config
        auto& config = getApp()->getConfig()["scene"];
        // If we have assets in the scene config, we deserialize them
        if(config.contains("assets")){
            our::deserializeAllAssets(config["assets"]);
        }
        // Build the world using the selected presets (if configured).
        const std::string carId = !getApp()->getSelectedCarPreset().empty() ? getApp()->getSelectedCarPreset()
            : (config.contains("selection") ? config["selection"].value("car", std::string{}) : std::string{});
        const std::string trackId = !getApp()->getSelectedTrackPreset().empty() ? getApp()->getSelectedTrackPreset()
            : (config.contains("selection") ? config["selection"].value("track", std::string{}) : std::string{});

        if(config.contains("presets")){
            world.deserialize(buildWorldWithPresets(config, carId, trackId));
        } else if(config.contains("world")){
            // Backwards-compatible path.
            world.deserialize(config["world"]);
        }
        // We initialize the camera controller system since it needs a pointer to the app
        cameraController.enter(getApp());
        carControllerSystem.enter(getApp());
        // Then we initialize the renderer
        auto size = getApp()->getFrameBufferSize();
        renderer.initialize(size, config["renderer"]);
    }

    void onDraw(double deltaTime) override {
        // Here, we just run a bunch of systems to control the world logic
        movementSystem.update(&world, (float)deltaTime);
        carControllerSystem.update(&world, (float)deltaTime);
        wheelSpinSystem.update(&world, (float)deltaTime);
        cameraController.update(&world, (float)deltaTime);
        chaseCameraSystem.update(&world, (float)deltaTime);
        // And finally we use the renderer system to draw the scene
        renderer.render(&world);

        // Get a reference to the keyboard object
        auto& keyboard = getApp()->getKeyboard();

        if(keyboard.justPressed(GLFW_KEY_ESCAPE)){
            // If the escape  key is pressed in this frame, go to the play state
            getApp()->changeState("menu");
        }
    }

    void onImmediateGui() override {
        // Top-left coordinate system HUD.
        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGuiWindowFlags flags = 0;
        flags |= ImGuiWindowFlags_NoDecoration;
        flags |= ImGuiWindowFlags_AlwaysAutoResize;
        flags |= ImGuiWindowFlags_NoSavedSettings;
        flags |= ImGuiWindowFlags_NoFocusOnAppearing;
        flags |= ImGuiWindowFlags_NoNav;

        if(ImGui::Begin("Coordinates", nullptr, flags)){
            auto* player = findEntityByName(world, "player");
            if(player){
                const glm::mat4 M = player->getLocalToWorldMatrix();
                const glm::vec3 p = glm::vec3(M * glm::vec4(0, 0, 0, 1));
                const float yawDeg = glm::degrees(player->localTransform.rotation.y);
                ImGui::Text("pos  x %.2f  y %.2f  z %.2f", p.x, p.y, p.z);
                ImGui::Text("yaw  %.1f deg", yawDeg);
            } else {
                ImGui::TextUnformatted("player not found");
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            // Simple 2D axis widget (world axes): +X right (red), +Y up-left (green), +Z down (blue)
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImVec2 origin(cursor.x + 18.0f, cursor.y + 18.0f);
            const float s = 28.0f;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(origin, 2.5f, IM_COL32(255, 255, 255, 220));

            const ImVec2 xEnd(origin.x + s, origin.y);
            const ImVec2 zEnd(origin.x, origin.y + s);
            const ImVec2 yEnd(origin.x + (-0.7f * s), origin.y + (-0.7f * s));

            dl->AddLine(origin, xEnd, IM_COL32(220, 60, 60, 255), 2.0f);
            dl->AddLine(origin, yEnd, IM_COL32(60, 220, 60, 255), 2.0f);
            dl->AddLine(origin, zEnd, IM_COL32(60, 120, 240, 255), 2.0f);

            dl->AddText(ImVec2(xEnd.x + 4.0f, xEnd.y - 8.0f), IM_COL32(220, 60, 60, 255), "X");
            dl->AddText(ImVec2(yEnd.x - 8.0f, yEnd.y - 12.0f), IM_COL32(60, 220, 60, 255), "Y");
            dl->AddText(ImVec2(zEnd.x + 4.0f, zEnd.y - 8.0f), IM_COL32(60, 120, 240, 255), "Z");

            // Reserve space for the widget so the window sizes correctly.
            ImGui::Dummy(ImVec2(2.0f * s, 1.4f * s));
        }
        ImGui::End();
    }

    void onDestroy() override {
        // Don't forget to destroy the renderer
        renderer.destroy();
        // On exit, we call exit for the camera controller system to make sure that the mouse is unlocked
        cameraController.exit();
        // Clear the world
        world.clear();
        // and we delete all the loaded assets to free memory on the RAM and the VRAM
        our::clearAllAssets();
    }
};