#include "light.hpp"

#include "../deserialize-utils.hpp"

#include <algorithm>
#include <cctype>

namespace our {

    static std::string toLower(std::string s){
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
        return s;
    }

    LightComponent::Type LightComponent::typeFromString(const std::string& s){
        const std::string v = toLower(s);
        if(v == "directional" || v == "dir" || v == "sun" || v == "moon") return Type::Directional;
        if(v == "spot" || v == "spotlight") return Type::Spot;
        return Type::Point;
    }

    void LightComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        lightType = typeFromString(data.value("lightType", "point"));

        color = data.value("color", glm::vec3(1.0f));
        intensity = data.value("intensity", 1.0f);

        attenuation = data.value("attenuation", glm::vec3(1.0f, 0.0f, 0.0f));

        innerAngle = data.value("innerAngle", innerAngle);
        outerAngle = data.value("outerAngle", outerAngle);
        if(outerAngle < innerAngle) outerAngle = innerAngle;

        positionOffset = data.value("positionOffset", positionOffset);

        castsShadows = data.value("castsShadows", false);
    }

}
