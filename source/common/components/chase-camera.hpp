#pragma once

#include "../ecs/component.hpp"

#include <glm/glm.hpp>

#include <string>

namespace our {

    // Locks a camera entity to follow a target entity in 3rd person.
    // Expected to be attached to the same entity that has CameraComponent.
    class ChaseCameraComponent : public Component {
    public:
        static std::string getID() { return "Chase Camera"; }

        // If empty, the system follows the first entity that has CarControllerComponent.
        std::string targetName;

        // Offset is in target local space (yaw only) and added to target position.
        glm::vec3 offset = glm::vec3(0.0f, 1.8f, -6.0f);

        // If enabled, camera rotation is locked to target yaw (plus 180 degrees)
        // with a fixed pitch, rather than using lookAt each frame.
        bool lockRotation = true;
        float pitch = -0.25f;

        // Camera looks at target position + rotated lookAtOffset.
        // If lookAtOffset is not provided in JSON, we fall back to lookAtHeight.
        glm::vec3 lookAtOffset = glm::vec3(0.0f, 0.8f, 0.0f);
        float lookAtHeight = 0.8f;

        float lookAroundYaw = 0.0f;          // current smoothed yaw orbit offset (radians)
        float lookAroundSmoothing = 12.0f;   // exponential lerp speed (higher = snappier)

        void deserialize(const nlohmann::json& data) override;
    };

}
