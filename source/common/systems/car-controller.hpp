#pragma once

#include "../application.hpp"
#include "../ecs/world.hpp"
#include "../components/car-controller.hpp"
#include "../components/track-heightfield.hpp"

#include <glm/gtx/euler_angles.hpp>

#include <algorithm>

namespace our {

    // Updates entities with CarControllerComponent using keyboard input,
    // and constrains them to a TrackHeightfieldComponent.
    class CarControllerSystem {
        Application* app = nullptr;

        static glm::vec3 getForward(float yaw){
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            // Many imported car OBJs face +Z in their local space.
            return glm::vec3(rot * glm::vec4(0, 0, 1, 0));
        }

        static bool sampleTrackHeight(World* world, float x, float z, float& outY){
            for(auto entity : world->getEntities()){
                if(auto* track = entity->getComponent<TrackHeightfieldComponent>()){
                    return track->sample(x, z, outY);
                }
            }
            return false;
        }

        static bool snapToTrack(World* world, glm::vec3& position, float clearance){
            float y;
            if(!sampleTrackHeight(world, position.x, position.z, y)) return false;
            position.y = y + clearance;
            return true;
        }

        static bool tryMove(World* world, glm::vec3& position, const glm::vec3& delta, float clearance){
            glm::vec3 candidate = position + delta;
            if(snapToTrack(world, candidate, clearance)){
                position = candidate;
                return true;
            }

            // Slide: try X-only then Z-only.
            candidate = position + glm::vec3(delta.x, 0.0f, 0.0f);
            if(snapToTrack(world, candidate, clearance)){
                position = candidate;
                return true;
            }

            candidate = position + glm::vec3(0.0f, 0.0f, delta.z);
            if(snapToTrack(world, candidate, clearance)){
                position = candidate;
                return true;
            }

            // No movement possible.
            return false;
        }

    public:
        void enter(Application* application){ app = application; }

        void update(World* world, float deltaTime){
            if(app == nullptr) return;

            auto& keyboard = app->getKeyboard();

            for(auto entity : world->getEntities()){
                auto* car = entity->getComponent<CarControllerComponent>();
                if(car == nullptr) continue;

                auto& transform = entity->localTransform;

                // Ensure starting position is snapped to the track.
                snapToTrack(world, transform.position, car->groundClearance);

                const float throttle = (keyboard.isPressed(GLFW_KEY_W) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_S) ? 1.0f : 0.0f);
                // Swapped controls: D = left, A = right
                const float steer = (keyboard.isPressed(GLFW_KEY_D) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_A) ? 1.0f : 0.0f);

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
                // Swap steering while reversing.
                const float reversing = (car->speed < -0.1f) ? -1.0f : 1.0f;
                transform.rotation.y -= (steer * reversing) * car->turnSpeed * deltaTime * (0.2f + 0.8f * speedFactor);

                // Integrate position in XZ.
                const glm::vec3 forward = getForward(transform.rotation.y);
                const glm::vec3 delta = forward * (car->speed * deltaTime);

                // Apply with track constraint.
                tryMove(world, transform.position, glm::vec3(delta.x, 0.0f, delta.z), car->groundClearance);
            }
        }
    };

}
