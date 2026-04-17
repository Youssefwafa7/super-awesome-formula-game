#pragma once

#include "../ecs/world.hpp"
#include "../components/chase-camera.hpp"
#include "../components/car-controller.hpp"

#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

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

    public:
        void update(World* world, float /*deltaTime*/){
            for(auto entity : world->getEntities()){
                auto* chase = entity->getComponent<ChaseCameraComponent>();
                if(chase == nullptr) continue;

                Entity* targetEntity = findTarget(world, chase->targetName);
                if(targetEntity == nullptr) continue;

                auto& camT = entity->localTransform;
                const auto& targetT = targetEntity->localTransform;

                // Use target yaw only for chase offset.
                const glm::mat4 yawRot = glm::yawPitchRoll(targetT.rotation.y, 0.0f, 0.0f);
                const glm::vec3 worldOffset = glm::vec3(yawRot * glm::vec4(chase->offset, 0.0f));

                camT.position = targetT.position + worldOffset;

                if(chase->lockRotation){
                    // Keep the camera rigidly attached to the target orientation.
                    // Camera forward is -Z, so we add pi to match target's +Z forward.
                    camT.rotation.x = chase->pitch;
                    camT.rotation.y = targetT.rotation.y + glm::pi<float>();
                    camT.rotation.z = 0.0f;
                } else {
                    const glm::vec3 lookAtPoint = targetT.position + glm::vec3(yawRot * glm::vec4(chase->lookAtOffset, 0.0f));
                    setLookAt(camT, camT.position, lookAtPoint);
                }
            }
        }
    };

}
