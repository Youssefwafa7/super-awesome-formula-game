#pragma once

#include "../ecs/world.hpp"
#include "../components/wheel-spin.hpp"
#include "../components/multi-mesh-renderer.hpp"
#include "../components/car-controller.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace our {

    // Spins wheel parts (sub-meshes) for entities that have WheelSpinComponent.
    class WheelSpinSystem {

        static std::string toLowerCopy(const std::string& s){
            std::string out;
            out.reserve(s.size());
            for(unsigned char ch : s) out.push_back((char)std::tolower(ch));
            return out;
        }

        static bool containsAnySubstringCI(const std::string& haystackLower, const std::vector<std::string>& needles){
            for(const auto& n : needles){
                if(n.empty()) continue;
                const std::string needleLower = toLowerCopy(n);
                if(haystackLower.find(needleLower) != std::string::npos) return true;
            }
            return false;
        }

        static glm::vec3 getForwardFromYaw(float yaw){
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            return glm::vec3(rot * glm::vec4(0, 0, 1, 0));
        }

        static int estimateWheelAxisLocal(const MultiMeshRendererComponent::Part& part){
            // Axis (axle direction) is assumed to be the thinnest AABB dimension.
            const glm::vec3 s = glm::abs(part.aabbSize);
            if(s.x <= s.y && s.x <= s.z) return 0;
            if(s.y <= s.x && s.y <= s.z) return 1;
            return 2;
        }

        static std::vector<int> autoDetectWheelParts(const MultiMeshRendererComponent& renderer, const WheelSpinComponent& spin){
            struct Candidate {
                int index;
                float score;
                glm::vec2 xz;
            };

            std::vector<Candidate> candidates;
            candidates.reserve(renderer.parts.size());

            const std::vector<std::string> defaultHints = {"wheel", "tire", "tyre", "rim"};

            for(int i = 0; i < (int)renderer.parts.size(); i++){
                const auto& part = renderer.parts[i];

                const std::string fullNameLower = toLowerCopy(part.objectName + std::string(" ") + part.materialName);
                if(!spin.excludeNameSubstrings.empty() && containsAnySubstringCI(fullNameLower, spin.excludeNameSubstrings)){
                    continue;
                }

                // Geometry heuristic: wheels are often "round" in two axes and thin in the third.
                const glm::vec3 s = glm::abs(part.aabbSize);
                const float a = s.x, b = s.y, c = s.z;
                const float maxDim = std::max(a, std::max(b, c));
                const float minDim = std::min(a, std::min(b, c));
                const float midDim = (a + b + c) - maxDim - minDim;
                if(maxDim <= 1e-6f || midDim <= 1e-6f) continue;

                const float roundness = std::clamp(midDim / maxDim, 0.0f, 1.0f);
                const float thinness = std::clamp(minDim / midDim, 0.0f, 1.0f);

                // Reject very "blocky" parts.
                if(roundness < 0.45f) continue;

                // Prefer parts near ground (lower pivot Y).
                const float groundBonus = -0.15f * part.localTransform.position.y;

                float nameBonus = 0.0f;
                // Name matching is a bonus (not a requirement), because many OBJs use non-semantic names like "obj8".
                if(!spin.includeNameSubstrings.empty() && containsAnySubstringCI(fullNameLower, spin.includeNameSubstrings)){
                    nameBonus = 3.0f;
                } else if(containsAnySubstringCI(fullNameLower, defaultHints)){
                    nameBonus = 1.5f;
                }

                float score = 0.0f;
                score += 2.0f * roundness;
                score += 1.0f * (1.0f - thinness);
                score += groundBonus;
                score += nameBonus;

                candidates.push_back({i, score, glm::vec2(part.localTransform.position.x, part.localTransform.position.z)});
            }

            if(candidates.empty()) return {};

            glm::vec2 center(0.0f);
            for(const auto& c : candidates) center += c.xz;
            center /= (float)candidates.size();

            const int desiredCount = std::max(1, spin.desiredWheelCount);

            std::vector<int> picked;
            picked.reserve(desiredCount);

            // Pick first: farthest from center (tie-breaker: score).
            int firstIndex = -1;
            float bestFirst = -1e9f;
            for(const auto& c : candidates){
                const float d = glm::length(c.xz - center);
                const float value = d + 0.2f * c.score;
                if(value > bestFirst){
                    bestFirst = value;
                    firstIndex = c.index;
                }
            }
            if(firstIndex < 0) return {};
            picked.push_back(firstIndex);

            while((int)picked.size() < desiredCount){
                int bestIdx = -1;
                float bestValue = -1e9f;

                for(const auto& c : candidates){
                    if(std::find(picked.begin(), picked.end(), c.index) != picked.end()) continue;

                    float minDist = std::numeric_limits<float>::infinity();
                    for(int p : picked){
                        const glm::vec2 pxz(renderer.parts[p].localTransform.position.x, renderer.parts[p].localTransform.position.z);
                        minDist = std::min(minDist, glm::length(c.xz - pxz));
                    }

                    const float value = minDist + 0.1f * c.score;
                    if(value > bestValue){
                        bestValue = value;
                        bestIdx = c.index;
                    }
                }

                if(bestIdx < 0) break;
                picked.push_back(bestIdx);
            }

            return picked;
        }

        static float estimateWheelRadiusLocal(const MultiMeshRendererComponent::Part& part, int axis){
            // If spinning around X, the wheel radius is roughly half of max(Y,Z).
            // Similar logic for other axes.
            const glm::vec3 s = glm::abs(part.aabbSize);
            if(axis == 0) return 0.5f * std::max(s.y, s.z);
            if(axis == 1) return 0.5f * std::max(s.x, s.z);
            return 0.5f * std::max(s.x, s.y);
        }

        static float estimateUniformScale(const Transform& t){
            return std::max(std::abs(t.scale.x), std::max(std::abs(t.scale.y), std::abs(t.scale.z)));
        }

        static void addAngle(Transform& t, int axis, float delta){
            if(axis == 0) t.rotation.x += delta;
            else if(axis == 1) t.rotation.y += delta;
            else t.rotation.z += delta;
        }

    public:
        void update(World* world, float deltaTime){
            if(world == nullptr) return;

            for(auto entity : world->getEntities()){
                auto* spin = entity->getComponent<WheelSpinComponent>();
                if(spin == nullptr) continue;

                auto* multi = entity->getComponent<MultiMeshRendererComponent>();
                if(multi == nullptr) continue;

                auto* car = entity->getComponent<CarControllerComponent>();

                if(spin->debugPrintParts && !spin->_printed){
                    std::cerr << "[WheelSpin] Parts for entity \"" << entity->name << "\":" << std::endl;
                    for(int i = 0; i < (int)multi->parts.size(); i++){
                        const auto& p = multi->parts[i];
                        std::cerr << "  [" << i << "] object=\"" << p.objectName << "\" material=\"" << p.materialName
                                  << "\" pivot=(" << p.localTransform.position.x << "," << p.localTransform.position.y << "," << p.localTransform.position.z << ")"
                                  << " size=(" << p.aabbSize.x << "," << p.aabbSize.y << "," << p.aabbSize.z << ")" << std::endl;
                    }
                    spin->_printed = true;
                }

                if(spin->partIndices.empty()){
                    spin->partIndices = autoDetectWheelParts(*multi, *spin);
                }

                const glm::vec3 currentPos = entity->localTransform.position;
                if(!spin->_hasLastPosition){
                    spin->_hasLastPosition = true;
                    spin->_lastWorldPosition = currentPos;
                    continue;
                }

                const glm::vec3 deltaPos = currentPos - spin->_lastWorldPosition;
                spin->_lastWorldPosition = currentPos;

                const glm::vec2 deltaXZ(deltaPos.x, deltaPos.z);
                const float dist = glm::length(deltaXZ);
                if(dist < 1e-6f) continue;

                float sign = 1.0f;
                if(car != nullptr){
                    if(car->speed < -1e-4f) sign = -1.0f;
                } else {
                    const glm::vec3 forward = getForwardFromYaw(entity->localTransform.rotation.y);
                    const glm::vec2 forwardXZ(forward.x, forward.z);
                    if(glm::length(forwardXZ) > 1e-6f){
                        const float d = glm::dot(glm::normalize(deltaXZ), glm::normalize(forwardXZ));
                        if(d < 0.0f) sign = -1.0f;
                    }
                }

                const float signedDistance = dist * sign;

                const float scale = estimateUniformScale(entity->localTransform);

                for(int idx : spin->partIndices){
                    if(idx < 0 || idx >= (int)multi->parts.size()) continue;
                    auto& part = multi->parts[idx];

                    const int axis = (spin->axis == -1) ? estimateWheelAxisLocal(part) : spin->axis;
                    const float radiusLocal = estimateWheelRadiusLocal(part, axis);
                    const float radiusWorld = std::max(1e-4f, radiusLocal * scale);

                    (void)deltaTime;
                    const float deltaAngle = (signedDistance / radiusWorld) * spin->direction;
                    addAngle(part.localTransform, axis, deltaAngle);
                }
            }
        }
    };

}
