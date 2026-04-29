#pragma once

#include "../ecs/component.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

        // Surface response tuning
        float grassSpeedFactor = 0.86f;      // soft max speed factor on grass
        float grassDamping = 2.0f;           // overspeed bleed rate on grass (1/sec)
        float grassTurnFactor = 0.84f;       // steering response factor on grass
        float grassAccelFactor = 0.90f;      // throttle/brake response factor on grass

        // Hard wall-collision tuning
        float wallBounceDamping = 0.32f;     // rebound speed factor on wall impact (0..1)
        float collisionSubstepDistance = 0.12f; // smaller = more stable collision at high speed
        float collisionRadius = 0.42f;       // XZ collision radius for wall segments
        float wallPushback = 0.03f;          // extra push away from walls after contact
        int wallResolveIterations = 4;       // wall depenetration iterations per move
        float maxClimbHeight = 0.22f;        // max upward height change allowed per sub-step

        // Visual-only front wheel steering animation
        float wheelSteerMaxAngle = 30.0f; // degrees

        float groundClearance = 0.08f; // how high the car stays above sampled track height

        // Surface alignment tuning (pitch/roll to match track slope)
        float slopeSmoothingSpeed = 8.0f;  // slerp rate toward target surface orientation
        float maxPitchAngle = 0.25f;       // ~14 degrees max pitch (subtle)
        float maxRollAngle = 0.4f;         // ~23 degrees max roll

        // Runtime state
        float speed = 0.0f;
        float steeringAngle = 0.0f; // radians (visual-only)
        bool noClip = false;

        // Smoothed surface orientation (runtime, not serialized)
        glm::quat _surfaceOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        // Cached part indices for front wheel steering (MultiMeshRenderer)
        bool _cachedFrontWheelParts = false;
        std::vector<int> _frontWheelPartIndices;
        std::vector<float> _frontWheelBaseYaw;

        void deserialize(const nlohmann::json& data) override;
    };

}
