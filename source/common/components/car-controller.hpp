#pragma once

#include "../ecs/component.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace our {

    // Semi-realistic kinematic car controller (W/S throttle, A/D steering).
    // Uses 2D velocity vectors, force-based acceleration, slip-angle tire model,
    // and force-derived steering instead of direct rotation.
    // Constrained to TrackHeightfieldComponent for vertical snapping.
    // Optionally animates front wheel steering if a MultiMeshRenderer has wheel parts.
    class CarControllerComponent : public Component {
    public:
        static std::string getID() { return "Car Controller"; }

        // ── Core physics state (runtime, not serialized) ──
        glm::vec2 velocity = {0.0f, 0.0f};  // World-space XZ velocity (m/s)
        float     yawRate  = 0.0f;           // Angular velocity (rad/s)

        // ── Engine / braking ──
        float engineForce     = 120.0f;   // Stronger forward acceleration (m/s²)
        float brakeForce      = 45.0f;    // Increased for more aggressive stopping power
        float maxSpeed        = 40.0f;    // Increased forward speed clamp (m/s)
        float maxReverseSpeed = 10.0f;    // Increased reverse speed clamp (m/s)

        // ── Friction ──
        float rollingResistance = 0.5f;   // Constant velocity-proportional drag (1/s)
        float aeroDragCoeff     = 0.05f;  // Drag proportional to v² (1/m)
        float lateralFriction   = 25.0f;  // Higher base friction for stability, will be reduced during skid/brake

        // ── Tire / slip model ──
        float maxGripSlipAngle  = 0.09f;  // Lower threshold to trigger slip sooner (rad, ~5°)
        float driftSlipAngle    = 0.35f;  // Slightly lower for earlier drift onset (rad, ~20°)
        float driftGripFactor   = 0.10f;  // Even less grip when drifting for stronger slide
        float driftAssist       = 0.5f;   // Reduced counter‑steer to allow drift

        // ── Steering ──
        float steerAngleMax     = 0.50f;  // rad (~28°)
        float steerSpeed        = 2.5f;   
        float steerReturnSpeed  = 6.0f;   
        float wheelbase         = 2.0f;   // Longer wheelbase for gentler turning radius
        float minSteerSpeed     = 1.0f;   // Require higher speed before yaw changes (m/s)
        float highSpeedSteerReduction = 0.5f; // Slightly more reduction at top speed

        // ── Angular stability ──
        float maxYawRate        = 3.5f;   // Slightly higher yaw cap for smoother turning
        float angularDamping    = 4.0f;   // Balanced damping

        // ── Surface response tuning (grass multipliers) ──
        float grassEngineScale    = 0.60f;  // Engine force multiplier on grass
        float grassLateralScale   = 0.50f;  // Lateral friction multiplier on grass
        float grassDragScale      = 1.8f;   // Rolling resistance multiplier on grass
        float grassMaxSpeedScale  = 0.70f;  // Max speed multiplier on grass

        // ── Hard wall-collision tuning ──
        float wallBounceDamping        = 0.32f;   // Velocity reflection coefficient (0..1)
        float wallBounceMinSpeed       = 0.15f;   // Below this, zero out instead of reflect
        float collisionSubstepDistance = 0.12f;    // Smaller = more stable collision at high speed
        float collisionRadius          = 0.42f;    // XZ collision radius for wall segments
        float wallPushback             = 0.03f;    // Extra push away from walls after contact
        int   wallResolveIterations    = 4;        // Wall depenetration iterations per move
        float maxClimbHeight           = 0.22f;    // Max upward height change per sub-step

        // ── Visual-only front wheel steering animation ──
        float wheelSteerMaxAngle = 30.0f; // degrees

        float groundClearance = 0.08f; // how high the car stays above sampled track height

        // ── Runtime state ──
        float currentSteerAngle = 0.0f;  // Current smoothed front wheel angle (rad)
        float steeringAngle     = 0.0f;  // Visual steering (rad, for front wheel anim)
        float speed             = 0.0f;  // Forward speed (backward compat for WheelSpin/HUD)
        bool  noClip            = false;

        // Cached part indices for front wheel steering (MultiMeshRenderer)
        bool _cachedFrontWheelParts = false;
        std::vector<int> _frontWheelPartIndices;
        std::vector<float> _frontWheelBaseYaw;

        void deserialize(const nlohmann::json& data) override;
    };

}
