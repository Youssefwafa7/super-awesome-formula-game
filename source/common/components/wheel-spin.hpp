#pragma once

#include "../ecs/component.hpp"

#include <vector>

#include <glm/vec3.hpp>

namespace our {

    // Rotates selected sub-mesh parts (typically wheels) based on how far the owning entity moved.
    // Intended to be used with MultiMeshRendererComponent where parts have per-part transforms.
    class WheelSpinComponent : public Component {
    public:
        static std::string getID() { return "Wheel Spin"; }

        // Axis to spin around, as an Euler axis selector: 0=x, 1=y, 2=z.
        // Use -1 to auto-pick an axis per-wheel (based on the thinnest AABB dimension).
        int axis = -1;

        // Multiplies the computed angle delta (use -1 to flip direction).
        float direction = 1.0f;

        // If empty, the system tries to auto-detect wheel-like parts.
        std::vector<int> partIndices;

        // If partIndices is empty, how many wheels to pick.
        int desiredWheelCount = 4;

        // Optional name hints to make selection robust across assets.
        // Matching is case-insensitive substring match against (objectName + materialName).
        std::vector<std::string> includeNameSubstrings;
        std::vector<std::string> excludeNameSubstrings;

        // If true, dumps part indices/names/sizes once to stderr (useful to configure indices).
        bool debugPrintParts = false;

        // Internal state
        bool _printed = false;

        // Used when no CarControllerComponent is present: spin based on travel distance.
        bool _hasLastPosition = false;
        glm::vec3 _lastWorldPosition = glm::vec3(0.0f);

        void deserialize(const nlohmann::json& data) override;
    };

}
