#pragma once

#include "../ecs/world.hpp"
#include "../components/chase-camera.hpp"
#include "../components/car-controller.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

namespace our {

    class ChaseCameraSystem {
        static Entity* findTarget(World* world, const std::string& targetName){
            if(!targetName.empty()){
                for(auto entity : world->getEntities()){
                    if(entity->name == targetName) return entity;
                }
            }
            for(auto entity : world->getEntities()){
                if(entity->getComponent<CarControllerComponent>() != nullptr) return entity;
            }
            return nullptr;
        }

        static void setLookAt(Transform& cameraTransform, const glm::vec3& cameraPos, const glm::vec3& target){
            const glm::vec3 dir = glm::normalize(target - cameraPos);

            // Camera forward is -Z in local space (see renderer).
            const float yaw = std::atan2(dir.x, -dir.z);
            const float pitch = std::asin(std::clamp(dir.y, -1.0f, 1.0f));

            cameraTransform.rotation.x = pitch;
            cameraTransform.rotation.y = yaw;
            cameraTransform.rotation.z = 0.0f;
        }

        static float computeLookAroundTarget(){
            GLFWgamepadstate state;
            if(!glfwGetGamepadState(GLFW_JOYSTICK_1, &state)) return 0.0f;

            const float rx = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
            const float ry = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];

            // Deadzone — ignore small inputs.
            const float deadzone = 0.35f;
            const float mag = std::sqrt(rx * rx + ry * ry);
            if(mag < deadzone) return 0.0f;

            // Stick angle in radians: 0 = right, π/2 = down, -π/2 = up, ±π = left.
            const float angle = std::atan2(ry, rx);

            // Snap zones (±30° tolerance around each cardinal direction):
            constexpr float pi = glm::pi<float>();
            constexpr float halfPi = pi * 0.5f;
            constexpr float snapTolerance = glm::radians(35.0f);

            // Stick RIGHT (angle ≈ 0) → camera orbits to the LEFT of car → positive yaw offset
            if(std::abs(angle) < snapTolerance){
                return -halfPi;
            }

            // Stick LEFT (angle ≈ ±π) → camera orbits to the RIGHT of car → negative yaw offset
            if(std::abs(angle) > pi - snapTolerance){
                return halfPi;
            }

            // Stick DOWN (angle ≈ +π/2) → look behind → yaw offset = π
            if(std::abs(angle - halfPi) < snapTolerance){
                return pi;
            }

            // Any other angle (including up): no look-around.
            return 0.0f;
        }

    public:
        void update(World* world, float deltaTime){
            for(auto entity : world->getEntities()){
                auto* chase = entity->getComponent<ChaseCameraComponent>();
                if(chase == nullptr) continue;

                Entity* targetEntity = findTarget(world, chase->targetName);
                if(targetEntity == nullptr) continue;

                // Only apply look-around for the player car (not AI).
                auto* targetCar = targetEntity->getComponent<CarControllerComponent>();
                const bool isPlayer = (targetCar != nullptr && !targetCar->isAI);

                // ── Compute look-around yaw offset ──
                float targetLookYaw = 0.0f;
                if(isPlayer){
                    targetLookYaw = computeLookAroundTarget();
                }

                // Smooth toward target look-around yaw.
                const float alpha = 1.0f - std::exp(-chase->lookAroundSmoothing * deltaTime);
                chase->lookAroundYaw += (targetLookYaw - chase->lookAroundYaw) * alpha;

                // Snap to zero if very close (avoids perpetual micro-drift).
                if(std::abs(chase->lookAroundYaw) < 0.001f && targetLookYaw == 0.0f){
                    chase->lookAroundYaw = 0.0f;
                }

                auto& camT = entity->localTransform;
                const auto& targetT = targetEntity->localTransform;

                // Apply look-around yaw on top of the target's heading.
                const float effectiveYaw = targetT.rotation.y + chase->lookAroundYaw;

                // Use target yaw (+ look-around orbit) for chase offset.
                const glm::mat4 yawRot = glm::yawPitchRoll(effectiveYaw, 0.0f, 0.0f);
                const glm::vec3 worldOffset = glm::vec3(yawRot * glm::vec4(chase->offset, 0.0f));

                camT.position = targetT.position + worldOffset;

                if(chase->lockRotation){
                    // Keep the camera rigidly attached to the target orientation.
                    // Camera forward is -Z, so we add pi to match target's +Z forward.
                    camT.rotation.x = chase->pitch;
                    camT.rotation.y = effectiveYaw + glm::pi<float>();
                    camT.rotation.z = 0.0f;
                } else {
                    const glm::vec3 lookAtPoint = targetT.position + glm::vec3(yawRot * glm::vec4(chase->lookAtOffset, 0.0f));
                    setLookAt(camT, camT.position, lookAtPoint);
                    camT.rotation.z = 0.0f;
                }
            }
        }
    };

}
