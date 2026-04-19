#pragma once

#include "../ecs/component.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace our {

    // Simple arcade car controller (W/S throttle, A/D steering).
    // Uses TrackHeightfieldComponent to keep the car on the track surface.
    // Also optionally animates front wheel steering if a MultiMeshRenderer has wheel parts.
    class CarControllerComponent : public Component {
    public:
        static std::string getID() { return "Car Controller"; }

        float acceleration = 12.0f;
        float brakeAcceleration = 18.0f;
        float maxSpeed = 18.0f;
        float maxReverseSpeed = 6.0f;
        float turnSpeed = 1.8f; // radians/sec
        float linearDamping = 4.0f;

        // Visual-only front wheel steering animation
        float wheelSteerMaxAngle = 30.0f; // degrees

        float groundClearance = 0.08f; // how high the car stays above sampled track height

        // Runtime state
        float speed = 0.0f;
        float steeringAngle = 0.0f; // radians (visual-only)

        // Cached part indices for front wheel steering (MultiMeshRenderer)
        bool _cachedFrontWheelParts = false;
        std::vector<int> _frontWheelPartIndices;
        std::vector<float> _frontWheelBaseYaw;

        void deserialize(const nlohmann::json& data) override;
    };

}
