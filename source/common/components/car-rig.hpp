#pragma once

#include "../ecs/component.hpp"
#include "../ecs/transform.hpp"

#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace our {

    // Per-wheel configuration loaded from JSON.
    // Each wheel may have a steer node (for Y-axis steering), any number of spin nodes
    // (for axle rotation), and optional static nodes (e.g. brake calipers) that must not rotate.
    struct WheelConfig {
        std::string label;                          // optional human-readable label
        std::string steerNode;                      // optional node name for steering pivot
        std::vector<std::string> spinNodes;         // node names that spin around the axle
        std::vector<std::string> staticNodes;       // node names that must NOT rotate
    };

    // Data-driven wheel rig component.
    // Replaces the old heuristic-based WheelSpinComponent by binding explicit node names
    // from a per-car JSON config to MultiMeshRenderer parts.
    class CarRigComponent : public Component {
    public:
        static std::string getID() { return "Car Rig"; }

        // --- Configuration (from JSON) ---

        std::vector<WheelConfig> wheels;

        // Spin axis: 0=X (default), 1=Y, 2=Z.  -1 = auto-detect per part.
        int spinAxis = 0;

        // Multiplier for spin direction (use -1.0 to flip).
        float spinDirection = 1.0f;

        // Maximum visual steering angle (degrees). Used only if CarControllerComponent
        // is not present (otherwise CarController computes steeringAngle itself).
        float steerMaxAngle = 30.0f;

        // If true, dumps resolved part info to stderr once (useful for debugging configs).
        bool debugPrint = false;

        // --- Runtime (resolved after GLTF load) ---

        struct SpinEntry {
            void* nodePtr = nullptr; // actually MultiMeshRendererComponent::Node*
            Transform originalTransform;
        };

        struct ResolvedWheel {
            void* steerNodePtr = nullptr; // actually MultiMeshRendererComponent::Node*
            Transform steerOriginalTransform;

            std::vector<SpinEntry> spinEntries;
            // static nodes are tracked only to log warnings; we never animate them.
        };

        std::vector<ResolvedWheel> resolvedWheels;
        bool _bound = false;
        bool _printed = false;

        // Animation state: accumulated spin angle in radians.
        float _accumulatedSpinAngle = 0.0f;

        // For positional-delta fallback (when no CarControllerComponent is present).
        bool _hasLastPosition = false;
        glm::vec3 _lastWorldPosition = glm::vec3(0.0f);

        void deserialize(const nlohmann::json& data) override;
    };

}
