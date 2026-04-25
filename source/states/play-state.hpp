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
#include <components/camera.hpp>
#include <components/car-controller.hpp>
#include <components/track-heightfield.hpp>

#include <algorithm>
#include <cmath>
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

    bool debugCollisionOverlayEnabled = true;
    bool debugDrawCarBox = true;
    bool debugDrawWallBoxes = true;
    bool debugDrawWallSegments = false;
    int debugWallRadiusCells = 28;
    int debugMaxWallBoxes = 1200;
    float debugWallBoxHeight = 0.9f;
    float debugLineThickness = 1.8f;

    struct CollisionDebugStats {
        int carBoxEdges = 0;
        int wallBoxesDrawn = 0;
        int wallBoxEdges = 0;
        int wallSegmentsDrawn = 0;
    };

    bool freeRoaming = false;

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

    static our::TrackHeightfieldComponent* findTrack(our::World& world){
        for(auto* e : world.getEntities()){
            if(e == nullptr) continue;
            if(auto* track = e->getComponent<our::TrackHeightfieldComponent>()) return track;
        }
        return nullptr;
    }

    static our::CameraComponent* findCamera(our::World& world){
        for(auto* e : world.getEntities()){
            if(e == nullptr) continue;
            if(auto* camera = e->getComponent<our::CameraComponent>()) return camera;
        }
        return nullptr;
    }

    static bool projectWorldToScreen(const glm::vec3& worldPos, const glm::mat4& VP, const ImVec2& displaySize, ImVec2& out){
        const glm::vec4 clip = VP * glm::vec4(worldPos, 1.0f);
        if(clip.w <= 1e-5f) return false;

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if(ndc.z < -1.0f || ndc.z > 1.0f) return false;

        out.x = (ndc.x * 0.5f + 0.5f) * displaySize.x;
        out.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * displaySize.y;
        return true;
    }

    static bool drawProjectedLine(
        ImDrawList* drawList,
        const glm::vec3& a,
        const glm::vec3& b,
        const glm::mat4& VP,
        const ImVec2& displaySize,
        ImU32 color,
        float thickness
    ) {
        ImVec2 a2, b2;
        if(!projectWorldToScreen(a, VP, displaySize, a2)) return false;
        if(!projectWorldToScreen(b, VP, displaySize, b2)) return false;
        drawList->AddLine(a2, b2, color, thickness);
        return true;
    }

    static int drawProjectedWireBox(
        ImDrawList* drawList,
        const glm::vec3& mn,
        const glm::vec3& mx,
        const glm::mat4& VP,
        const ImVec2& displaySize,
        ImU32 color,
        float thickness
    ) {
        const glm::vec3 corners[8] = {
            glm::vec3(mn.x, mn.y, mn.z), glm::vec3(mx.x, mn.y, mn.z),
            glm::vec3(mx.x, mn.y, mx.z), glm::vec3(mn.x, mn.y, mx.z),
            glm::vec3(mn.x, mx.y, mn.z), glm::vec3(mx.x, mx.y, mn.z),
            glm::vec3(mx.x, mx.y, mx.z), glm::vec3(mn.x, mx.y, mx.z)
        };

        static const int edges[12][2] = {
            {0,1}, {1,2}, {2,3}, {3,0},
            {4,5}, {5,6}, {6,7}, {7,4},
            {0,4}, {1,5}, {2,6}, {3,7}
        };

        int drawn = 0;
        for(const auto& edge : edges){
            if(drawProjectedLine(drawList, corners[edge[0]], corners[edge[1]], VP, displaySize, color, thickness)) drawn++;
        }
        return drawn;
    }

    CollisionDebugStats drawCollisionDebugOverlay(){
        CollisionDebugStats stats{};
        if(!debugCollisionOverlayEnabled) return stats;

        auto* camera = findCamera(world);
        auto* track = findTrack(world);
        auto* player = findEntityByName(world, "player");
        if(camera == nullptr || track == nullptr || player == nullptr) return stats;

        auto* car = player->getComponent<our::CarControllerComponent>();
        if(car == nullptr) return stats;

        const glm::ivec2 fbSize = getApp()->getFrameBufferSize();
        if(fbSize.x <= 0 || fbSize.y <= 0) return stats;

        const glm::mat4 VP = camera->getProjectionMatrix(fbSize) * camera->getViewMatrix();
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImDrawList* drawList = ImGui::GetForegroundDrawList();

        const glm::vec3 playerPos = player->localTransform.position;
        const float groundY = playerPos.y - car->groundClearance;

        if(debugDrawCarBox){
            const float r = std::max(0.01f, car->collisionRadius);
            const float halfH = std::max(0.25f, car->groundClearance + car->maxClimbHeight);

            const glm::vec3 carMin(playerPos.x - r, groundY, playerPos.z - r);
            const glm::vec3 carMax(playerPos.x + r, groundY + 2.0f * halfH, playerPos.z + r);
            stats.carBoxEdges += drawProjectedWireBox(
                drawList, carMin, carMax, VP, displaySize,
                IM_COL32(255, 220, 80, 235), debugLineThickness
            );

            const float expandedR = r + std::max(0.0f, track->wallCollisionMargin) + std::max(0.0f, car->wallPushback);
            if(expandedR > r + 1e-4f){
                const glm::vec3 contactMin(playerPos.x - expandedR, groundY, playerPos.z - expandedR);
                const glm::vec3 contactMax(playerPos.x + expandedR, groundY + 2.0f * halfH, playerPos.z + expandedR);
                stats.carBoxEdges += drawProjectedWireBox(
                    drawList, contactMin, contactMax, VP, displaySize,
                    IM_COL32(255, 120, 80, 220), std::max(1.0f, debugLineThickness - 0.3f)
                );
            }
        }

        if(debugDrawWallBoxes && track->width > 0 && track->height > 0 && !track->wall.empty()){
            const int px = (int)std::floor((playerPos.x - track->minX) / track->cellSize);
            const int pz = (int)std::floor((playerPos.z - track->minZ) / track->cellSize);

            const int radius = std::max(1, debugWallRadiusCells);
            const int x0 = std::max(0, px - radius);
            const int x1 = std::min(track->width - 1, px + radius);
            const int z0 = std::max(0, pz - radius);
            const int z1 = std::min(track->height - 1, pz + radius);

            for(int z = z0; z <= z1; z++){
                for(int x = x0; x <= x1; x++){
                    if(stats.wallBoxesDrawn >= std::max(1, debugMaxWallBoxes)) break;

                    const int idx = z * track->width + x;
                    if(idx < 0 || idx >= (int)track->wall.size()) continue;
                    if(track->wall[(size_t)idx] == 0) continue;

                    const float wx0 = track->minX + (float)x * track->cellSize;
                    const float wz0 = track->minZ + (float)z * track->cellSize;
                    const float wx1 = wx0 + track->cellSize;
                    const float wz1 = wz0 + track->cellSize;

                    const float yBase =
                        (idx >= 0 && idx < (int)track->heights.size() && std::isfinite(track->heights[(size_t)idx]))
                        ? track->heights[(size_t)idx]
                        : groundY;

                    const glm::vec3 boxMin(wx0, yBase, wz0);
                    const glm::vec3 boxMax(wx1, yBase + std::max(0.1f, debugWallBoxHeight), wz1);

                    stats.wallBoxEdges += drawProjectedWireBox(
                        drawList, boxMin, boxMax, VP, displaySize,
                        IM_COL32(255, 70, 70, 225), std::max(1.0f, debugLineThickness - 0.2f)
                    );
                    stats.wallBoxesDrawn++;
                }
            }
        }

        if(debugDrawWallSegments && !track->wallSegments.empty()){
            const float maxDist = std::max(1.0f, (float)debugWallRadiusCells * track->cellSize);
            const float maxDist2 = maxDist * maxDist;

            for(const auto& seg : track->wallSegments){
                const glm::vec2 mid = 0.5f * (seg.a + seg.b);
                const glm::vec2 d = glm::vec2(playerPos.x, playerPos.z) - mid;
                if(glm::dot(d, d) > maxDist2) continue;

                const glm::vec3 a(seg.a.x, groundY + 0.03f, seg.a.y);
                const glm::vec3 b(seg.b.x, groundY + 0.03f, seg.b.y);
                if(drawProjectedLine(drawList, a, b, VP, displaySize, IM_COL32(80, 220, 255, 230), debugLineThickness)){
                    stats.wallSegmentsDrawn++;
                }
            }
        }

        return stats;
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
        
        auto& keyboard = getApp()->getKeyboard();
        if(keyboard.justPressed(GLFW_KEY_F)){
            freeRoaming = !freeRoaming;
            auto* camera = findEntityByName(world, "main_camera");
            if(camera){
                if(freeRoaming){
                    auto* fcc = camera->addComponent<our::FreeCameraControllerComponent>();
                    fcc->positionSensitivity = {15.0f, 15.0f, 15.0f};
                    fcc->speedupFactor = 4.0f;
                } else {
                    camera->deleteComponent<our::FreeCameraControllerComponent>();
                }
            }
        }

        if(!freeRoaming){
            carControllerSystem.update(&world, (float)deltaTime);
            chaseCameraSystem.update(&world, (float)deltaTime);
        }
        
        wheelSpinSystem.update(&world, (float)deltaTime);
        cameraController.update(&world, (float)deltaTime);
        // And finally we use the renderer system to draw the scene
        renderer.render(&world);

        if(keyboard.justPressed(GLFW_KEY_ESCAPE)){
            // If the escape  key is pressed in this frame, go to the play state
            getApp()->changeState("menu");
        }
    }

    void onImmediateGui() override {
        const CollisionDebugStats debugStats = drawCollisionDebugOverlay();

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
            our::Entity* target = freeRoaming ? findEntityByName(world, "main_camera") : findEntityByName(world, "player");
            if(target){
                const glm::mat4 M = target->getLocalToWorldMatrix();
                const glm::vec3 p = glm::vec3(M * glm::vec4(0, 0, 0, 1));
                const float yawDeg = glm::degrees(target->localTransform.rotation.y);
                if(freeRoaming) ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "[FREE ROAM]");
                ImGui::Text("pos  x %.2f  y %.2f  z %.2f", p.x, p.y, p.z);
                ImGui::Text("yaw  %.1f deg", yawDeg);
            } else {
                ImGui::TextUnformatted("target not found");
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

        // Bottom-left speedometer HUD.
        {
            const ImVec2 display = ImGui::GetIO().DisplaySize;

            ImGui::SetNextWindowPos(ImVec2(10.0f, std::max(10.0f, display.y - 70.0f)), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);

            ImGuiWindowFlags spdFlags = 0;
            spdFlags |= ImGuiWindowFlags_NoDecoration;
            spdFlags |= ImGuiWindowFlags_AlwaysAutoResize;
            spdFlags |= ImGuiWindowFlags_NoSavedSettings;
            spdFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
            spdFlags |= ImGuiWindowFlags_NoNav;

            if(ImGui::Begin("Speed", nullptr, spdFlags)){
                auto* player = findEntityByName(world, "player");
                if(player){
                    auto* car = player->getComponent<our::CarControllerComponent>();
                    if(car){
                        const float speedMS = std::abs(car->speed);
                        const float speedKMH = speedMS * 3.6f;
                        const bool reversing = (car->speed < -0.1f);
                        ImGui::Text("%s  %.1f km/h", reversing ? "R" : "D", speedKMH);
                    } else {
                        ImGui::TextUnformatted("no car controller");
                    }
                } else {
                    ImGui::TextUnformatted("player not found");
                }
            }
            ImGui::End();
        }

        // Top-right collision debug HUD.
        {
            const ImVec2 display = ImGui::GetIO().DisplaySize;

            ImGui::SetNextWindowPos(ImVec2(std::max(10.0f, display.x - 320.0f), 10.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);

            ImGuiWindowFlags dbgFlags = 0;
            dbgFlags |= ImGuiWindowFlags_NoDecoration;
            dbgFlags |= ImGuiWindowFlags_AlwaysAutoResize;
            dbgFlags |= ImGuiWindowFlags_NoSavedSettings;
            dbgFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
            dbgFlags |= ImGuiWindowFlags_NoNav;

            if(ImGui::Begin("Collision Debug", nullptr, dbgFlags)){
                ImGui::Checkbox("Enable Overlay", &debugCollisionOverlayEnabled);
                ImGui::Checkbox("Draw Car Collision Box", &debugDrawCarBox);
                ImGui::Checkbox("Draw Wall Cell Boxes", &debugDrawWallBoxes);
                ImGui::Checkbox("Draw Wall Segments", &debugDrawWallSegments);
                ImGui::SliderInt("Wall Radius (cells)", &debugWallRadiusCells, 4, 80);
                ImGui::SliderInt("Max Wall Boxes", &debugMaxWallBoxes, 100, 4000);
                ImGui::SliderFloat("Wall Box Height", &debugWallBoxHeight, 0.1f, 2.5f, "%.2f");
                ImGui::SliderFloat("Line Thickness", &debugLineThickness, 1.0f, 4.0f, "%.1f");
                ImGui::Separator();
                ImGui::Text("car edges: %d", debugStats.carBoxEdges);
                ImGui::Text("wall boxes: %d", debugStats.wallBoxesDrawn);
                ImGui::Text("wall box edges: %d", debugStats.wallBoxEdges);
                ImGui::Text("wall segments: %d", debugStats.wallSegmentsDrawn);
            }
            ImGui::End();
        }
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