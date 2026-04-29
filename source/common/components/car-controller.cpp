#include "car-controller.hpp"

#include <algorithm>

namespace our {

    void CarControllerComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        acceleration = data.value("acceleration", acceleration);
        brakeAcceleration = data.value("brakeAcceleration", brakeAcceleration);
        maxSpeed = data.value("maxSpeed", maxSpeed);
        maxReverseSpeed = data.value("maxReverseSpeed", maxReverseSpeed);
        turnSpeed = data.value("turnSpeed", turnSpeed);
        linearDamping = data.value("linearDamping", linearDamping);
        grassSpeedFactor = data.value("grassSpeedFactor", grassSpeedFactor);
        grassDamping = data.value("grassDamping", grassDamping);
        grassTurnFactor = data.value("grassTurnFactor", grassTurnFactor);
        grassAccelFactor = data.value("grassAccelFactor", grassAccelFactor);
        wallBounceDamping = data.value("wallBounceDamping", wallBounceDamping);
        collisionSubstepDistance = data.value("collisionSubstepDistance", collisionSubstepDistance);
        collisionRadius = data.value("collisionRadius", collisionRadius);
        wallPushback = data.value("wallPushback", wallPushback);
        wallResolveIterations = data.value("wallResolveIterations", wallResolveIterations);
        maxClimbHeight = data.value("maxClimbHeight", maxClimbHeight);
        wheelSteerMaxAngle = data.value("wheelSteerMaxAngle", wheelSteerMaxAngle);
        groundClearance = data.value("groundClearance", groundClearance);
        slopeSmoothingSpeed = data.value("slopeSmoothingSpeed", slopeSmoothingSpeed);
        maxPitchAngle = data.value("maxPitchAngle", maxPitchAngle);
        maxRollAngle = data.value("maxRollAngle", maxRollAngle);

        grassSpeedFactor = std::max(0.2f, grassSpeedFactor);
        grassDamping = std::max(0.0f, grassDamping);
        grassTurnFactor = std::clamp(grassTurnFactor, 0.1f, 1.0f);
        grassAccelFactor = std::clamp(grassAccelFactor, 0.1f, 1.0f);
        wallBounceDamping = std::clamp(wallBounceDamping, 0.0f, 1.0f);
        collisionSubstepDistance = std::max(0.03f, collisionSubstepDistance);
        collisionRadius = std::max(0.05f, collisionRadius);
        wallPushback = std::max(0.0f, wallPushback);
        wallResolveIterations = std::max(1, wallResolveIterations);
        maxClimbHeight = std::max(0.02f, maxClimbHeight);
    }

}
