#pragma once

#include "../ecs/component.hpp"

#include <glm/vec3.hpp>

namespace our {

    class LightComponent : public Component {
    public:
        enum class Type {
            Directional = 0,
            Point = 1,
            Spot = 2
        };

        Type lightType = Type::Point;

        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;

        // (constant, linear, quadratic)
        glm::vec3 attenuation = glm::vec3(1.0f, 0.0f, 0.0f);

        // Spot light cone angles (degrees)
        float innerAngle = 15.0f;
        float outerAngle = 25.0f;

        // Local-space position offset from the owning entity origin.
        glm::vec3 positionOffset = glm::vec3(0.0f);

        // By default, light direction follows the owning entity's local forward (-Z).
        // Set this to true to flip the direction 180 degrees (useful for headlights that point backwards due to model orientation).
        bool invertDirection = false;

        bool castsShadows = false;

        static std::string getID() { return "Light"; }

        void deserialize(const nlohmann::json& data) override;

        static Type typeFromString(const std::string& s);
    };

}
