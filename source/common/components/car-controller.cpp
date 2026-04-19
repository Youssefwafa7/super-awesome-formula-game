#include "car-controller.hpp"

namespace our {

    void CarControllerComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        acceleration = data.value("acceleration", acceleration);
        brakeAcceleration = data.value("brakeAcceleration", brakeAcceleration);
        maxSpeed = data.value("maxSpeed", maxSpeed);
        maxReverseSpeed = data.value("maxReverseSpeed", maxReverseSpeed);
        turnSpeed = data.value("turnSpeed", turnSpeed);
        linearDamping = data.value("linearDamping", linearDamping);
        wheelSteerMaxAngle = data.value("wheelSteerMaxAngle", wheelSteerMaxAngle);
        groundClearance = data.value("groundClearance", groundClearance);
    }

}
