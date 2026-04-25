#include "car-controller.hpp"

#include <algorithm>

namespace our {

    void CarControllerComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        // ── Engine / braking ──
        engineForce     = data.value("engineForce", engineForce);
        brakeForce      = data.value("brakeForce", brakeForce);
        maxSpeed        = data.value("maxSpeed", maxSpeed);
        maxReverseSpeed = data.value("maxReverseSpeed", maxReverseSpeed);

        // Backward compatibility: map old arcade keys to new physics fields.
        if(data.contains("acceleration") && !data.contains("engineForce")){
            engineForce = data.value("acceleration", engineForce);
        }
        if(data.contains("brakeAcceleration") && !data.contains("brakeForce")){
            brakeForce = data.value("brakeAcceleration", brakeForce);
        }

        // ── Friction ──
        rollingResistance = data.value("rollingResistance", rollingResistance);
        aeroDragCoeff     = data.value("aeroDragCoeff", aeroDragCoeff);
        lateralFriction   = data.value("lateralFriction", lateralFriction);

        // Backward compatibility: old linearDamping → rollingResistance
        if(data.contains("linearDamping") && !data.contains("rollingResistance")){
            rollingResistance = data.value("linearDamping", rollingResistance);
        }

        // ── Tire / slip model ──
        maxGripSlipAngle = data.value("maxGripSlipAngle", maxGripSlipAngle);
        driftSlipAngle   = data.value("driftSlipAngle", driftSlipAngle);
        driftGripFactor  = data.value("driftGripFactor", driftGripFactor);
        driftAssist      = data.value("driftAssist", driftAssist);

        // ── Steering ──
        steerAngleMax     = data.value("steerAngleMax", steerAngleMax);
        steerSpeed        = data.value("steerSpeed", steerSpeed);
        steerReturnSpeed  = data.value("steerReturnSpeed", steerReturnSpeed);
        wheelbase         = data.value("wheelbase", wheelbase);
        minSteerSpeed     = data.value("minSteerSpeed", minSteerSpeed);
        highSpeedSteerReduction = data.value("highSpeedSteerReduction", highSpeedSteerReduction);

        // Backward compatibility: old turnSpeed can seed steer responsiveness
        // (ignored if new keys are present)

        // ── Angular stability ──
        maxYawRate      = data.value("maxYawRate", maxYawRate);
        angularDamping  = data.value("angularDamping", angularDamping);

        // ── Surface response (grass) ──
        grassEngineScale   = data.value("grassEngineScale", grassEngineScale);
        grassLateralScale  = data.value("grassLateralScale", grassLateralScale);
        grassDragScale     = data.value("grassDragScale", grassDragScale);
        grassMaxSpeedScale = data.value("grassMaxSpeedScale", grassMaxSpeedScale);

        // Backward compatibility: map old grass keys if new ones aren't present
        if(data.contains("grassAccelFactor") && !data.contains("grassEngineScale")){
            grassEngineScale = data.value("grassAccelFactor", grassEngineScale);
        }
        if(data.contains("grassSpeedFactor") && !data.contains("grassMaxSpeedScale")){
            grassMaxSpeedScale = data.value("grassSpeedFactor", grassMaxSpeedScale);
        }
        if(data.contains("grassTurnFactor") && !data.contains("grassLateralScale")){
            grassLateralScale = data.value("grassTurnFactor", grassLateralScale);
        }
        if(data.contains("grassDamping") && !data.contains("grassDragScale")){
            grassDragScale = data.value("grassDamping", grassDragScale);
        }

        // ── Wall collision ──
        wallBounceDamping        = data.value("wallBounceDamping", wallBounceDamping);
        wallBounceMinSpeed       = data.value("wallBounceMinSpeed", wallBounceMinSpeed);
        collisionSubstepDistance = data.value("collisionSubstepDistance", collisionSubstepDistance);
        collisionRadius          = data.value("collisionRadius", collisionRadius);
        wallPushback             = data.value("wallPushback", wallPushback);
        wallResolveIterations    = data.value("wallResolveIterations", wallResolveIterations);
        maxClimbHeight           = data.value("maxClimbHeight", maxClimbHeight);

        // ── Visual ──
        wheelSteerMaxAngle = data.value("wheelSteerMaxAngle", wheelSteerMaxAngle);
        groundClearance    = data.value("groundClearance", groundClearance);

        // ── Clamp/validate ──
        engineForce     = std::max(0.0f, engineForce);
        brakeForce      = std::max(0.0f, brakeForce);
        maxSpeed        = std::max(1.0f, maxSpeed);
        maxReverseSpeed = std::max(0.5f, maxReverseSpeed);

        rollingResistance = std::max(0.0f, rollingResistance);
        aeroDragCoeff     = std::max(0.0f, aeroDragCoeff);
        lateralFriction   = std::max(0.0f, lateralFriction);

        maxGripSlipAngle = std::clamp(maxGripSlipAngle, 0.01f, 1.0f);
        driftSlipAngle   = std::max(maxGripSlipAngle + 0.01f, driftSlipAngle);
        driftGripFactor  = std::clamp(driftGripFactor, 0.0f, 1.0f);
        driftAssist      = std::max(0.0f, driftAssist);

        steerAngleMax    = std::clamp(steerAngleMax, 0.05f, 1.2f);
        steerSpeed       = std::max(0.5f, steerSpeed);
        steerReturnSpeed = std::max(0.5f, steerReturnSpeed);
        wheelbase        = std::max(0.1f, wheelbase);
        minSteerSpeed    = std::max(0.0f, minSteerSpeed);
        highSpeedSteerReduction = std::clamp(highSpeedSteerReduction, 0.05f, 1.0f);

        maxYawRate     = std::max(0.5f, maxYawRate);
        angularDamping = std::max(0.0f, angularDamping);

        grassEngineScale   = std::clamp(grassEngineScale, 0.1f, 1.0f);
        grassLateralScale  = std::clamp(grassLateralScale, 0.1f, 1.0f);
        grassDragScale     = std::max(0.1f, grassDragScale);
        grassMaxSpeedScale = std::clamp(grassMaxSpeedScale, 0.2f, 1.0f);

        wallBounceDamping        = std::clamp(wallBounceDamping, 0.0f, 1.0f);
        wallBounceMinSpeed       = std::max(0.0f, wallBounceMinSpeed);
        collisionSubstepDistance = std::max(0.03f, collisionSubstepDistance);
        collisionRadius          = std::max(0.05f, collisionRadius);
        wallPushback             = std::max(0.0f, wallPushback);
        wallResolveIterations    = std::max(1, wallResolveIterations);
        maxClimbHeight           = std::max(0.02f, maxClimbHeight);
    }

}
