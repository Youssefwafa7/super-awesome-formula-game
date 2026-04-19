#pragma once

#include "../application.hpp"
#include "../ecs/world.hpp"
#include "../components/car-controller.hpp"
#include "../components/track-heightfield.hpp"
#include "../components/multi-mesh-renderer.hpp"

#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace our {

    // Updates entities with CarControllerComponent using keyboard input,
    // and constrains them to a TrackHeightfieldComponent.
    //
    // Controls:
    // - W/S: forward/reverse
    // - A/D: steering
    class CarControllerSystem {
        Application* app = nullptr;

        static glm::vec3 getForward(float yaw){
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            // Many imported car OBJs face +Z in their local space.
            return glm::vec3(rot * glm::vec4(0, 0, 1, 0));
        }

        static std::string toLowerCopy(const std::string& s){
            std::string out;
            out.reserve(s.size());
            for(unsigned char ch : s) out.push_back((char)std::tolower(ch));
            return out;
        }

        static TrackHeightfieldComponent* findTrack(World* world){
            if(world == nullptr) return nullptr;
            for(auto entity : world->getEntities()){
                if(auto* track = entity->getComponent<TrackHeightfieldComponent>()) return track;
            }
            return nullptr;
        }

        static bool snapToTrack(const TrackHeightfieldComponent* track, glm::vec3& position, float clearance){
            if(track == nullptr) return false;
            float y;
            if(!track->sample(position.x, position.z, y)) return false;
            position.y = y + clearance;
            return true;
        }

        static bool tryMove(const TrackHeightfieldComponent* track, glm::vec3& position, const glm::vec3& delta, float clearance){
            glm::vec3 candidate = position + delta;
            if(snapToTrack(track, candidate, clearance)){
                position = candidate;
                return true;
            }

            // Slide: try X-only then Z-only.
            candidate = position + glm::vec3(delta.x, 0.0f, 0.0f);
            if(snapToTrack(track, candidate, clearance)){
                position = candidate;
                return true;
            }

            candidate = position + glm::vec3(0.0f, 0.0f, delta.z);
            if(snapToTrack(track, candidate, clearance)){
                position = candidate;
                return true;
            }

            // No movement possible.
            return false;
        }

        static void cacheFrontWheelParts(CarControllerComponent& car, const MultiMeshRendererComponent& multi){
            if(car._cachedFrontWheelParts) return;

            struct Candidate { int idx; float score; glm::vec3 p; };
            std::vector<Candidate> candidates;
            candidates.reserve(multi.parts.size());

            const std::vector<std::string> wheelHints = {"wheel", "tire", "tyre", "rim"};

            for(int i = 0; i < (int)multi.parts.size(); i++){
                const auto& part = multi.parts[i];
                const std::string fullNameLower = toLowerCopy(part.objectName + std::string(" ") + part.materialName);

                // Geometry heuristic: wheels are often round in two axes and thin in the third.
                const glm::vec3 s = glm::abs(part.aabbSize);
                const float a = s.x, b = s.y, c = s.z;
                const float maxDim = std::max(a, std::max(b, c));
                const float minDim = std::min(a, std::min(b, c));
                const float midDim = (a + b + c) - maxDim - minDim;
                if(maxDim <= 1e-6f || midDim <= 1e-6f) continue;

                const float roundness = std::clamp(midDim / maxDim, 0.0f, 1.0f);
                const float thinness = std::clamp(minDim / midDim, 0.0f, 1.0f);
                if(roundness < 0.45f) continue;

                float nameBonus = 0.0f;
                for(const auto& h : wheelHints){
                    if(fullNameLower.find(h) != std::string::npos){ nameBonus = 1.0f; break; }
                }

                float score = 0.0f;
                score += 2.0f * roundness;
                score += 1.0f * (1.0f - thinness);
                score += nameBonus;
                // Prefer parts near ground.
                score += -0.1f * part.localTransform.position.y;

                candidates.push_back({i, score, part.localTransform.position});
            }

            if(candidates.size() < 2){
                car._cachedFrontWheelParts = true;
                return;
            }

            // Pick up to 4 best wheel candidates.
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b){ return a.score > b.score; });
            if(candidates.size() > 8) candidates.resize(8);

            // Compute center, then pick farthest points.
            glm::vec3 center(0.0f);
            for(const auto& c : candidates) center += c.p;
            center /= (float)candidates.size();

            std::vector<int> picked;
            picked.reserve(4);
            while(picked.size() < 4 && !candidates.empty()){
                int bestIdx = -1;
                float bestVal = -1e9f;
                for(const auto& c : candidates){
                    if(std::find(picked.begin(), picked.end(), c.idx) != picked.end()) continue;
                    const float dist = glm::length(glm::vec2(c.p.x - center.x, c.p.z - center.z));
                    const float val = dist + 0.15f * c.score;
                    if(val > bestVal){ bestVal = val; bestIdx = c.idx; }
                }
                if(bestIdx < 0) break;
                picked.push_back(bestIdx);
            }

            if(picked.size() < 2){
                car._cachedFrontWheelParts = true;
                return;
            }

            // Front wheels: those with highest local Z (assuming car forward is +Z).
            std::vector<std::pair<float,int>> byZ;
            byZ.reserve(picked.size());
            for(int idx : picked){
                byZ.push_back({multi.parts[idx].localTransform.position.z, idx});
            }
            std::sort(byZ.begin(), byZ.end(), [](auto a, auto b){ return a.first > b.first; });

            car._frontWheelPartIndices.clear();
            car._frontWheelBaseYaw.clear();
            const int count = std::min(2, (int)byZ.size());
            for(int i = 0; i < count; i++){
                const int idx = byZ[i].second;
                car._frontWheelPartIndices.push_back(idx);
                car._frontWheelBaseYaw.push_back(multi.parts[idx].localTransform.rotation.y);
            }

            car._cachedFrontWheelParts = true;
        }

    public:
        void enter(Application* application){ app = application; }

        void update(World* world, float deltaTime){
            if(app == nullptr) return;

            auto* track = findTrack(world);

            auto& keyboard = app->getKeyboard();

            for(auto entity : world->getEntities()){
                auto* car = entity->getComponent<CarControllerComponent>();
                if(car == nullptr) continue;

                auto& transform = entity->localTransform;

                // Ensure starting position is snapped to the track.
                snapToTrack(track, transform.position, car->groundClearance);

                const float throttle = (keyboard.isPressed(GLFW_KEY_W) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_S) ? 1.0f : 0.0f);
                const float steer = (keyboard.isPressed(GLFW_KEY_A) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_D) ? 1.0f : 0.0f);

                // Update speed.
                if(throttle > 0.0f){
                    car->speed += car->acceleration * deltaTime;
                } else if(throttle < 0.0f){
                    car->speed -= car->brakeAcceleration * deltaTime;
                } else {
                    // Damping towards 0.
                    const float damping = std::max(0.0f, 1.0f - car->linearDamping * deltaTime);
                    car->speed *= damping;
                }

                car->speed = std::clamp(car->speed, -car->maxReverseSpeed, car->maxSpeed);

                // Turning. (Less turning when nearly stopped.)
                const float speedFactor = std::clamp(std::abs(car->speed) / std::max(1e-3f, car->maxSpeed), 0.0f, 1.0f);
                // If the car is basically stationary, don't rotate in place (only steer tires visually).
                if(std::abs(car->speed) > 0.05f){
                    // Swap steering while reversing.
                    const float reversing = (car->speed < -0.1f) ? -1.0f : 1.0f;
                    transform.rotation.y += (steer * reversing) * car->turnSpeed * deltaTime * speedFactor;
                }

                // Integrate position in XZ.
                const glm::vec3 forward = getForward(transform.rotation.y);
                const glm::vec3 delta = forward * (car->speed * deltaTime);

                // Apply with track constraint.
                tryMove(track, transform.position, glm::vec3(delta.x, 0.0f, delta.z), car->groundClearance);

                // Visual wheel steering angle (independent of movement).
                car->steeringAngle = steer * glm::radians(car->wheelSteerMaxAngle);

                // Front wheel steering animation (if MultiMeshRenderer exists).
                if(auto* multi = entity->getComponent<MultiMeshRendererComponent>()){
                    cacheFrontWheelParts(*car, *multi);
                    for(size_t i = 0; i < car->_frontWheelPartIndices.size(); i++){
                        const int idx = car->_frontWheelPartIndices[i];
                        if(idx < 0 || idx >= (int)multi->parts.size()) continue;
                        multi->parts[idx].localTransform.rotation.y = car->_frontWheelBaseYaw[i] + car->steeringAngle;
                    }
                }
            }
        }
    };

}
