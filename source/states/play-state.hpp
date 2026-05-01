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
#include "miniaudio.h"
#include <deserialize-utils.hpp>

#include <string>
#include <fstream>
#include <filesystem>
#include <sstream>

// This state shows how to use the ECS framework and deserialization.
class Playstate: public our::State {

    our::World world;
    our::ForwardRenderer renderer;
    our::FreeCameraControllerSystem cameraController;
    our::MovementSystem movementSystem;
    our::CarControllerSystem carControllerSystem;
    our::ChaseCameraSystem chaseCameraSystem;
    our::WheelSpinSystem wheelSpinSystem;

    bool debugCollisionOverlayEnabled = false;
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

    // ── Audio ──
    ma_engine audioEngine;
    ma_sound playMusic;
    ma_sound countdownSound;
    bool isAudioInitialized = false;
    bool isMusicLoaded = false;
    bool isCountdownSoundLoaded = false;
    float introAudioDuration = 2.5f;
    bool introAudioDone = false;

    // ── Countdown Timer ──
    float countdownTimer = 3.0f; 
    bool isRaceStarted = false;

    // ── Checkpoint System ──
    struct Checkpoint {
        glm::vec3 pos;
        float radius;
        bool hit = false;
    };

    struct AiRacer {
        std::string name;
        float lapTime;
        float totalTime;
    };

    std::vector<Checkpoint> checkpoints;
    int nextCheckpointIndex = 0;
    int currentLap = 1;
    int totalLaps = 3;
    float currentLapTime = 0.0f;
    float bestLapTime = 0.0f;
    float totalRaceTime = 0.0f;
    float totalPenaltyTime = 0.0f;
    int playerPosition = 1;
    std::vector<AiRacer> aiRacers;
    bool raceFinished = false;
    bool crossedStartLine = false;
    int lastHitIdx = -1;

    std::string currentTrackId;
    float checkpointRadius = 25.0f;
    std::string lastCheckpointStatus;

    // Number of AI cars to spawn
    static constexpr int kNumAICars = 2;

    // ── Auto-recording system ──
    bool isRecording = false;
    std::vector<glm::vec3> recordedPositions;
    glm::vec3 lastRecordedPos = glm::vec3(0.0f);
    float recordDistanceInterval = 3.0f; // sample every 3 units of travel

    void loadCheckpoints() {
        checkpoints.clear();
        lastCheckpointStatus = "";
        std::string filename = "assets/tracks/" + currentTrackId + "_checkpoints.csv";
        
        try {
            if (!std::filesystem::exists(filename)) {
                lastCheckpointStatus = "No checkpoints file found.";
                return;
            }

            std::ifstream file(filename);
            if (!file.is_open()) return;

            std::string line;
            if (!std::getline(file, line)) return; // Skip header

            while (std::getline(file, line)) {
                if (line.empty()) continue;
                std::stringstream ss(line);
                std::string x_str, y_str, z_str, r_str;
                if (std::getline(ss, x_str, ',') &&
                    std::getline(ss, y_str, ',') &&
                    std::getline(ss, z_str, ',') &&
                    std::getline(ss, r_str, ',')) {
                    try {
                        Checkpoint cp;
                        cp.pos = {std::stof(x_str), std::stof(y_str), std::stof(z_str)};
                        cp.radius = std::stof(r_str);
                        cp.hit = false;
                        checkpoints.push_back(cp);
                    } catch (...) {
                        // Skip malformed lines
                    }
                }
            }
            file.close();
            nextCheckpointIndex = 0;
            if (checkpoints.empty()) {
                lastCheckpointStatus = "Checkpoints file is empty or malformed.";
            }
        } catch (...) {
            lastCheckpointStatus = "Unknown error loading checkpoints.";
        }
    }

    void initAiRacers() {
        aiRacers.clear();
        float baseTime = 60.0f;
        if (currentTrackId == "montreal") baseTime = 80.0f;
        else if (currentTrackId == "silverstone") baseTime = 100.0f;
        else if (currentTrackId == "spa") baseTime = 130.0f;

        for (int i = 0; i < kNumAICars; i++) {
            AiRacer ai;
            ai.name = "AI Racer " + std::to_string(i + 1);
            ai.lapTime = baseTime + (float)(std::rand() % 1000 - 500) / 100.0f;
            ai.totalTime = 0.0f;
            aiRacers.push_back(ai);
        }
    }

    // Spawn AI car entities by creating them programmatically and sharing
    // the player's mesh/material data (avoids re-loading the GLTF model).
    void spawnAICars() {
        if (checkpoints.empty()) return; // No checkpoints → no AI

        auto* player = findEntityByName(world, "player");
        if (!player) return;

        auto* playerMulti = player->getComponent<our::MultiMeshRendererComponent>();
        auto* playerCar = player->getComponent<our::CarControllerComponent>();
        if (!playerMulti || !playerCar) return;

        const glm::vec3 spawnPos = player->localTransform.position;
        const float spawnYaw = player->localTransform.rotation.y;

        // Forward and right directions at spawn.
        const glm::vec3 fwd(std::sin(spawnYaw), 0.0f, std::cos(spawnYaw));
        const glm::vec3 right(fwd.z, 0.0f, -fwd.x);

        const float rowSpacing = 15.0f;
        const float colSpacing = 8.0f;

        // Different tint multipliers for AI cars.
        const glm::vec4 aiTints[] = {
            {0.2f, 0.5f, 1.0f, 1.0f},  // blue
            {1.0f, 0.3f, 0.3f, 1.0f},  // red
            {0.3f, 1.0f, 0.3f, 1.0f},  // green
        };

        // Get starting grid from track config
        const auto& fullConfig = getApp()->getConfig();
        const auto& sceneConfig = fullConfig["scene"];
        const auto* trackPreset = findPresetById(sceneConfig["presets"]["tracks"], currentTrackId);
        const nlohmann::json* spawnArray = (trackPreset && trackPreset->contains("spawn") && (*trackPreset)["spawn"].is_array()) 
                                            ? &((*trackPreset)["spawn"]) : nullptr;

        for (int i = 0; i < kNumAICars; i++) {
            try {
                our::Entity* aiEnt = world.add();
                aiEnt->name = "ai_car_" + std::to_string(i);
                aiEnt->parent = nullptr;

                if (spawnArray && i < (int)spawnArray->size()) {
                    const auto& s = (*spawnArray)[i];
                    aiEnt->localTransform.position = s.value("position", glm::vec3(0.0f));
                    aiEnt->localTransform.rotation = glm::radians(s.value("rotation", glm::vec3(0.0f)));
                } else {
                    // Fallback to relative positioning if no grid defined
                    int row = i / 2 + 1;
                    int col = (i % 2 == 0) ? -1 : 1;
                    glm::vec3 offset = -fwd * (rowSpacing * (float)row) + right * (colSpacing * (float)col * 0.5f);
                    aiEnt->localTransform.position = spawnPos + offset;
                    aiEnt->localTransform.rotation = player->localTransform.rotation;
                }
                aiEnt->localTransform.scale = player->localTransform.scale;

                // ── Car Controller (copy tuning from player) ──
                auto* aiCarCtrl = aiEnt->addComponent<our::CarControllerComponent>();
                // Make AI cars slightly slower and give them more turn speed (tighter turning radius)
                // so they can follow the track easier without sliding out.
                aiCarCtrl->acceleration = playerCar->acceleration * 0.8f;
                aiCarCtrl->brakeAcceleration = playerCar->brakeAcceleration * 1.4f; // Stop faster
                aiCarCtrl->maxSpeed = playerCar->maxSpeed * 0.8f;
                aiCarCtrl->maxReverseSpeed = playerCar->maxReverseSpeed;
                aiCarCtrl->turnSpeed = playerCar->turnSpeed * 1.5f;
                aiCarCtrl->linearDamping = playerCar->linearDamping;
                aiCarCtrl->grassSpeedFactor = playerCar->grassSpeedFactor;
                aiCarCtrl->grassDamping = playerCar->grassDamping;
                aiCarCtrl->grassTurnFactor = playerCar->grassTurnFactor;
                aiCarCtrl->grassAccelFactor = playerCar->grassAccelFactor;
                aiCarCtrl->wallBounceDamping = playerCar->wallBounceDamping;
                aiCarCtrl->collisionSubstepDistance = playerCar->collisionSubstepDistance;
                aiCarCtrl->collisionRadius = playerCar->collisionRadius;
                aiCarCtrl->wallPushback = playerCar->wallPushback;
                aiCarCtrl->wallResolveIterations = playerCar->wallResolveIterations;
                aiCarCtrl->maxClimbHeight = playerCar->maxClimbHeight;
                aiCarCtrl->wheelSteerMaxAngle = playerCar->wheelSteerMaxAngle;
                aiCarCtrl->groundClearance = playerCar->groundClearance;
                aiCarCtrl->slopeSmoothingSpeed = playerCar->slopeSmoothingSpeed;
                aiCarCtrl->maxPitchAngle = playerCar->maxPitchAngle;
                aiCarCtrl->maxRollAngle = playerCar->maxRollAngle;

                // AI-specific state
                aiCarCtrl->isAI = true;
                aiCarCtrl->nextCheckpointIndex = 0;
                aiCarCtrl->currentLap = 1;
                aiCarCtrl->crossedStartLine = false;
                aiCarCtrl->lastHitIdx = -1;
                aiCarCtrl->aiLateralOffset = (i % 2 == 0 ? 1.0f : -1.0f) * (1.0f + (float)i * 0.5f);
                aiCarCtrl->aiRandomSeed = (float)(i + 1) / (float)(kNumAICars + 1);

                // ── Multi Mesh Renderer (share mesh/material data from player) ──
                auto* aiMulti = aiEnt->addComponent<our::MultiMeshRendererComponent>();
                aiMulti->borrowedFromSource = true; // Don't delete shared resources!
                aiMulti->sourceObjPath = playerMulti->sourceObjPath;
                aiMulti->mergeByMaterial = playerMulti->mergeByMaterial;

                // Copy all parts — mesh/material pointers are shared (owned by player).
                aiMulti->parts.reserve(playerMulti->parts.size());
                for (const auto& srcPart : playerMulti->parts) {
                    our::MultiMeshRendererComponent::Part p;
                    p.mesh = srcPart.mesh;
                    p.material = srcPart.material;
                    p.objectName = srcPart.objectName;
                    p.materialName = srcPart.materialName;
                    p.localTransform = srcPart.localTransform;
                    p.aabbSize = srcPart.aabbSize;
                    aiMulti->parts.push_back(std::move(p));
                }

            } catch (...) {
                // Silently skip if any AI car fails to spawn.
            }
        }
    }

    void updateRaceLogic(float deltaTime) {
        if (!isRaceStarted || raceFinished || checkpoints.empty()) return;

        struct Ranker {
            our::Entity* entity;
            float progress;
        };
        std::vector<Ranker> carRanks;

        for (auto* entity : world.getEntities()) {
            auto* car = entity->getComponent<our::CarControllerComponent>();
            if (!car) continue;

            glm::vec3 carPos = entity->localTransform.position;

            // Player-specific logic (checkpoint detection)
            // AI checkpoint detection is handled in CarControllerSystem::computeAIInput
            if (!car->isAI) {
                if (!car->crossedStartLine) {
                    float dist = glm::distance(carPos, checkpoints[0].pos);
                    if (dist < checkpoints[0].radius) {
                        car->crossedStartLine = true;
                        car->lastHitIdx = 0;
                        car->nextCheckpointIndex = 1 % checkpoints.size();
                        if (entity->name == "player") {
                            totalRaceTime = 0.0f;
                            currentLapTime = 0.0f;
                        }
                    }
                } else {
                    int searchRange = std::max(1, (int)checkpoints.size() / 2);
                    int foundIdx = -1;
                    for (int i = 0; i < searchRange; ++i) {
                        int idx = (car->nextCheckpointIndex + i) % checkpoints.size();
                        float dist = glm::distance(carPos, checkpoints[idx].pos);
                        if (dist < checkpoints[idx].radius) {
                            foundIdx = idx;
                            break;
                        }
                    }

                    if (foundIdx != -1 && foundIdx != car->lastHitIdx) {
                        // Calculate how many were skipped
                        int numSkipped = 0;
                        if (foundIdx >= car->nextCheckpointIndex) {
                            numSkipped = foundIdx - car->nextCheckpointIndex;
                        } else {
                            // Skipping across the finish line (wrap around)
                            numSkipped = ((int)checkpoints.size() - car->nextCheckpointIndex) + foundIdx;
                        }

                        if (numSkipped > 0 && !car->isAI) {
                            totalPenaltyTime += (float)numSkipped * 20.0f; 
                        }

                        if (foundIdx < car->nextCheckpointIndex || (foundIdx == 0 && car->nextCheckpointIndex == 0)) {
                            car->currentLap++;
                            if (entity->name == "player") {
                                if (bestLapTime == 0.0f || currentLapTime < bestLapTime) bestLapTime = currentLapTime;
                                if (car->currentLap > totalLaps) {
                                    raceFinished = true;
                                } else {
                                    currentLapTime = 0.0f;
                                }
                            }
                        }
                        car->lastHitIdx = foundIdx;
                        car->nextCheckpointIndex = (foundIdx + 1) % checkpoints.size();
                    }
                }
            }

            // Calculate progress for ranking
            float progress = (float)car->currentLap * 10000.0f;
            progress += (float)car->nextCheckpointIndex * 100.0f;

            // Finer resolution: distance to next checkpoint
            int targetIdx = car->nextCheckpointIndex;
            int prevIdx = (targetIdx + (int)checkpoints.size() - 1) % (int)checkpoints.size();
            float distToNext = glm::distance(carPos, checkpoints[targetIdx].pos);
            float segLen = glm::distance(checkpoints[prevIdx].pos, checkpoints[targetIdx].pos);
            if (segLen > 0.1f) {
                progress += (1.0f - std::clamp(distToNext / segLen, 0.0f, 1.0f)) * 100.0f;
            }

            carRanks.push_back({entity, progress});
        }

        // Sort cars by progress
        std::sort(carRanks.begin(), carRanks.end(), [](const Ranker& a, const Ranker& b) {
            return a.progress > b.progress;
        });

        // Find player rank and sync state
        for (int i = 0; i < (int)carRanks.size(); ++i) {
            if (carRanks[i].entity->name == "player") {
                playerPosition = i + 1;
                auto* pCar = carRanks[i].entity->getComponent<our::CarControllerComponent>();
                currentLap = pCar->currentLap;
                crossedStartLine = pCar->crossedStartLine;
                nextCheckpointIndex = pCar->nextCheckpointIndex;
                lastHitIdx = pCar->lastHitIdx;
                break;
            }
        }

        if (isRaceStarted && !raceFinished) {
            totalRaceTime += deltaTime;
            currentLapTime += deltaTime;
        }
    }

    void saveCheckpoint(bool isStart) {
        if (currentTrackId.empty()) {
            lastCheckpointStatus = "Error: No track ID";
            return;
        }

        namespace fs = std::filesystem;
        try {
            fs::create_directories("assets/tracks");
            std::string filename = "assets/tracks/" + currentTrackId + "_checkpoints.csv";
            
            std::ofstream file;
            if (isStart) {
                file.open(filename, std::ios::out | std::ios::trunc);
                if (file.is_open()) {
                    file << "x,y,z,radius\n";
                }
            } else {
                file.open(filename, std::ios::out | std::ios::app);
            }

            if (!file.is_open()) {
                lastCheckpointStatus = "Error: Could not open file " + filename;
                return;
            }

            auto* player = findEntityByName(world, "player");
            if (!player) {
                lastCheckpointStatus = "Error: Player not found";
                return;
            }

            glm::vec3 pos = player->localTransform.position;
            file << pos.x << "," << pos.y << "," << pos.z << "," << checkpointRadius << "\n";
            file.close();

            lastCheckpointStatus = (isStart ? "Track start recorded" : "Checkpoint added");
        } catch (const std::exception& e) {
            lastCheckpointStatus = "Error: " + std::string(e.what());
        }
    }

    // Save the auto-recorded lap to the checkpoints CSV, replacing the old data.
    void saveRecordedLap() {
        if (currentTrackId.empty() || recordedPositions.size() < 5) {
            lastCheckpointStatus = "Not enough data (" + std::to_string(recordedPositions.size()) + " points)";
            return;
        }

        namespace fs = std::filesystem;
        try {
            fs::create_directories("assets/tracks");

            // Save raw recording to _recorded.csv (does not overwrite AI checkpoints)
            {
                std::string rawFile = "assets/tracks/" + currentTrackId + "_recorded.csv";
                std::ofstream out(rawFile, std::ios::out | std::ios::trunc);
                if (out.is_open()) {
                    out << "x,y,z,radius\n";
                    for (const auto& p : recordedPositions) {
                        out << p.x << "," << p.y << "," << p.z << ",12\n";
                    }
                    out.close();
                }
            }

            lastCheckpointStatus = "Saved " + std::to_string(recordedPositions.size()) + " recorded points!";

        } catch (const std::exception& e) {
            lastCheckpointStatus = "Error: " + std::string(e.what());
        }
    }

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
        const nlohmann::json* carPreset = nullptr;
        if(presets.contains("cars") && presets["cars"].is_array()){
            carPreset = findPresetById(presets["cars"], carId);
            if(!carPreset) carPreset = firstPreset(presets["cars"]);
        }
        
        const nlohmann::json* trackPreset = nullptr;
        if(presets.contains("tracks") && presets["tracks"].is_array()){
            trackPreset = findPresetById(presets["tracks"], trackId);
            if(!trackPreset) trackPreset = firstPreset(presets["tracks"]);
        }


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

            auto readVec3 = [](const nlohmann::json& value, const glm::vec3& fallback){
                if(!value.is_array() || value.size() < 3) return fallback;
                return glm::vec3(
                    value[0].get<float>(),
                    value[1].get<float>(),
                    value[2].get<float>()
                );
            };

            // Override spawn from track preset if provided.
            if(trackPreset != nullptr && trackPreset->contains("spawn")){
                const auto& spawn = (*trackPreset)["spawn"];
                const nlohmann::json* playerSpawn = nullptr;
                
                if(spawn.is_array() && spawn.size() >= 3){
                    playerSpawn = &spawn[2]; // Use index 2 for player
                } else if(spawn.is_object()){
                    playerSpawn = &spawn;
                }

                if(playerSpawn){
                    if(playerSpawn->contains("position") && player.contains("position") && player["position"].is_array() && (*playerSpawn)["position"].is_array()){
                        const glm::vec3 playerPosition = readVec3(player["position"], glm::vec3(0.0f));
                        const glm::vec3 spawnPosition = readVec3((*playerSpawn)["position"], glm::vec3(0.0f));
                        const glm::vec3 combinedPosition = playerPosition + spawnPosition;
                        player["position"] = nlohmann::json::array({combinedPosition.x, combinedPosition.y, combinedPosition.z});
                    } else if(playerSpawn->contains("position")) {
                        player["position"] = (*playerSpawn)["position"];
                    }

                    if(playerSpawn->contains("rotation") && player.contains("rotation") && player["rotation"].is_array() && (*playerSpawn)["rotation"].is_array()){
                        const glm::vec3 playerRotation = readVec3(player["rotation"], glm::vec3(0.0f));
                        const glm::vec3 spawnRotation = readVec3((*playerSpawn)["rotation"], glm::vec3(0.0f));
                        const glm::vec3 combinedRotation = playerRotation + spawnRotation;
                        player["rotation"] = nlohmann::json::array({combinedRotation.x, combinedRotation.y, combinedRotation.z});
                    } else if(playerSpawn->contains("rotation")) {
                        player["rotation"] = (*playerSpawn)["rotation"];
                    }
                }
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
        currentTrackId = trackId;

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

        // Initialize Audio Engine and start music
        if (ma_engine_init(NULL, &audioEngine) == MA_SUCCESS) {
            isAudioInitialized = true;
            if (ma_sound_init_from_file(&audioEngine, "assets/audio/17. Into the Pipe (Hurry Up!).mp3", 0, NULL, NULL, &playMusic) == MA_SUCCESS) {
                isMusicLoaded = true;
                ma_sound_set_looping(&playMusic, MA_FALSE);
                ma_sound_set_stop_time_in_milliseconds(&playMusic, (ma_uint64)(introAudioDuration * 1000.0f));
                ma_sound_start(&playMusic);
            }
            if (ma_sound_init_from_file(&audioEngine, "assets/audio/Mario Kart Race Countdown - Sound Effect.mp3", 0, NULL, NULL, &countdownSound) == MA_SUCCESS) {
                isCountdownSoundLoaded = true;
                ma_sound_set_looping(&countdownSound, MA_FALSE);
            }
        }
        
        // Reset countdown timer when entering the play state
        countdownTimer = 3.0f;
        isRaceStarted = false;
        introAudioDone = false;

        raceFinished = false;
        currentLap = 1;
        currentLapTime = 0.0f;
        totalRaceTime = 0.0f;
        totalPenaltyTime = 0.0f;
        nextCheckpointIndex = 0;
        crossedStartLine = false;
        lastHitIdx = -1;
        loadCheckpoints();
        initAiRacers();
        
        // Reset player component race state
        if(auto* playerEnt = findEntityByName(world, "player")){
            if(auto* pCar = playerEnt->getComponent<our::CarControllerComponent>()){
                pCar->currentLap = 1;
                pCar->nextCheckpointIndex = 0;
                pCar->crossedStartLine = false;
                pCar->lastHitIdx = -1;
            }
        }

        // Spawn AI-controlled cars (requires checkpoints to be loaded first).
        spawnAICars();
    }

    void onDraw(double deltaTime) override {
        // Update countdown timer
        bool isAudioPlaying = false;
        if (isMusicLoaded) {
            isAudioPlaying = ma_sound_is_playing(&playMusic);
        }

        if (!introAudioDone && !isAudioPlaying) {
            introAudioDone = true;
            if (isCountdownSoundLoaded) {
                ma_sound_start(&countdownSound);
            }
        }

        if (introAudioDone && !isRaceStarted) {
            countdownTimer -= (float)deltaTime;
            if (countdownTimer <= -1.0f) {
                countdownTimer = -1.0f;
                isRaceStarted = true;
            }
        }

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
        
        if(keyboard.justPressed(GLFW_KEY_N)){
            saveCheckpoint(false);
        }

        // ── Auto-recording toggle (R key) ──
        if(keyboard.justPressed(GLFW_KEY_R)){
            if(!isRecording){
                // Start recording.
                isRecording = true;
                recordedPositions.clear();
                auto* player = findEntityByName(world, "player");
                if(player){
                    lastRecordedPos = player->localTransform.position;
                    recordedPositions.push_back(lastRecordedPos);
                }
                lastCheckpointStatus = "RECORDING started — drive a lap!";
            } else {
                // Stop recording and save.
                isRecording = false;
                saveRecordedLap();
            }
        }

        // Auto-sample while recording.
        if(isRecording){
            auto* player = findEntityByName(world, "player");
            if(player){
                const glm::vec3 pos = player->localTransform.position;
                const float dist = glm::distance(
                    glm::vec2(pos.x, pos.z),
                    glm::vec2(lastRecordedPos.x, lastRecordedPos.z)
                );
                if(dist >= recordDistanceInterval){
                    recordedPositions.push_back(pos);
                    lastRecordedPos = pos;
                }
            }
        }

        // Build checkpoint positions vector for AI cars.
        std::vector<glm::vec3> cpPositions;
        cpPositions.reserve(checkpoints.size());
        for (const auto& cp : checkpoints) cpPositions.push_back(cp.pos);

        // Update car controller system (handles snapping and position alignment even during countdown)
        if(!freeRoaming){
            carControllerSystem.update(&world, (float)deltaTime, isRaceStarted, cpPositions);
        }

        updateRaceLogic((float)deltaTime);
        
        // Always update chase camera so it follows the car from the start
        if(!freeRoaming){
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
                ImGui::Separator();
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            } else {
                ImGui::TextUnformatted("target not found");
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
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
                        const float speedKMH = speedMS * 3.6f * 1.5f;
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

        // Countdown timer display
        if (!isRaceStarted && introAudioDone) {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            
            ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.0f);
            
            ImGuiWindowFlags countFlags = 0;
            countFlags |= ImGuiWindowFlags_NoDecoration;
            countFlags |= ImGuiWindowFlags_AlwaysAutoResize;
            countFlags |= ImGuiWindowFlags_NoSavedSettings;
            countFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
            countFlags |= ImGuiWindowFlags_NoNav;

            if (ImGui::Begin("Countdown", nullptr, countFlags)) {
                ImGui::SetWindowFontScale(4.0f);
                if (countdownTimer <= 0.0f) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "GO!");
                } else {
                    int seconds = (int)std::ceil(countdownTimer);
                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "%d", seconds);
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

        // Checkpoint System HUD
        {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(std::max(10.0f, display.x - 320.0f), 300.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowBgAlpha(0.35f);
            
            ImGuiWindowFlags cpFlags = 0;
            cpFlags |= ImGuiWindowFlags_NoDecoration;
            cpFlags |= ImGuiWindowFlags_AlwaysAutoResize;
            cpFlags |= ImGuiWindowFlags_NoSavedSettings;
            cpFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
            cpFlags |= ImGuiWindowFlags_NoNav;

            if(ImGui::Begin("Checkpoint System", nullptr, cpFlags)){
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Checkpoint System (%s)", currentTrackId.c_str());
                ImGui::SliderFloat("Radius", &checkpointRadius, 5.0f, 100.0f, "%.1f");
                if(ImGui::Button("Record Track Start", ImVec2(-1, 0))){
                    saveCheckpoint(true);
                }
                if(ImGui::Button("Add Checkpoint (N)", ImVec2(-1, 0))){
                    saveCheckpoint(false);
                }
                ImGui::Separator();
                // Auto-recording controls.
                if(isRecording){
                    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "** RECORDING ** (%d pts)", (int)recordedPositions.size());
                    if(ImGui::Button("Stop & Save (R)", ImVec2(-1, 0))){
                        isRecording = false;
                        saveRecordedLap();
                    }
                } else {
                    if(ImGui::Button("Auto-Record Lap (R)", ImVec2(-1, 0))){
                        isRecording = true;
                        recordedPositions.clear();
                        auto* player2 = findEntityByName(world, "player");
                        if(player2){
                            lastRecordedPos = player2->localTransform.position;
                            recordedPositions.push_back(lastRecordedPos);
                        }
                        lastCheckpointStatus = "RECORDING started - drive a lap!";
                    }
                }
                if(!lastCheckpointStatus.empty()){
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", lastCheckpointStatus.c_str());
                }
            }
            ImGui::End();
        }

        // Race HUD
        {
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, 30.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.35f);
            
            ImGuiWindowFlags raceFlags = 0;
            raceFlags |= ImGuiWindowFlags_NoDecoration;
            raceFlags |= ImGuiWindowFlags_AlwaysAutoResize;
            raceFlags |= ImGuiWindowFlags_NoSavedSettings;
            raceFlags |= ImGuiWindowFlags_NoFocusOnAppearing;
            raceFlags |= ImGuiWindowFlags_NoNav;

            if(ImGui::Begin("Race HUD", nullptr, raceFlags)){
                ImGui::SetWindowFontScale(1.5f);
                if (!crossedStartLine) {
                    ImGui::SetWindowFontScale(2.0f);
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "CROSS THE START LINE!");
                } else {
                    ImGui::SetWindowFontScale(3.0f); // Even bigger
                    
                    ImGui::Text("LAP: %d / %d", std::min(currentLap, totalLaps), totalLaps);
                    ImGui::SameLine();
                    ImGui::Dummy(ImVec2(150.0f, 0.0f)); // Fixed large gap
                    ImGui::SameLine();
                    ImGui::Text("POS: %d / %d", playerPosition, (int)aiRacers.size() + 1);
                }
                
                ImGui::Separator();
                
                ImGui::Text("Time: %.2f", currentLapTime);
                if(bestLapTime > 0) {
                    ImGui::SameLine(200);
                    ImGui::Text("Best: %.2f", bestLapTime);
                }
                
                if(totalPenaltyTime > 0) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Penalty: +%.1fs", totalPenaltyTime);
                }

                if(raceFinished) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "RACE FINISHED!");
                    ImGui::Text("Final Rank: %d", playerPosition);
                }
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

        if (isMusicLoaded) {
            ma_sound_uninit(&playMusic);
            isMusicLoaded = false;
        }
        if (isCountdownSoundLoaded) {
            ma_sound_uninit(&countdownSound);
            isCountdownSoundLoaded = false;
        }
        if (isAudioInitialized) {
            ma_engine_uninit(&audioEngine);
            isAudioInitialized = false;
        }
    }
};