#pragma once

#include "../application.hpp"
#include "../ecs/world.hpp"
#include "../components/car-controller.hpp"
#include "../components/track-heightfield.hpp"
#include "../components/multi-mesh-renderer.hpp"

#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace our {

    class CarControllerSystem {
        Application* app = nullptr;

        static float resolveVerticalTarget(float currentY, float sampledTargetY, float maxClimbHeight, bool touchedWall){
            if(sampledTargetY > currentY && touchedWall) return currentY;
            if(sampledTargetY <= currentY) return sampledTargetY;
            if(sampledTargetY > currentY + maxClimbHeight) return currentY;
            return sampledTargetY;
        }

        static glm::vec3 getForward(float yaw){
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            return glm::vec3(rot * glm::vec4(0, 0, 1, 0));
        }

        static glm::vec3 getRight(float yaw){
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            return glm::vec3(rot * glm::vec4(1, 0, 0, 0));
        }

        static std::string toLowerCopy(const std::string& s){
            std::string out;
            out.reserve(s.size());
            for(unsigned char ch : s) out.push_back((char)std::tolower(ch));
            return out;
        }

        static bool containsAnySubstringCI(const std::string& haystackLower, const std::vector<std::string>& needles){
            for(const auto& n : needles){
                if(n.empty()) continue;
                const std::string needleLower = toLowerCopy(n);
                if(haystackLower.find(needleLower) != std::string::npos) return true;
            }
            return false;
        }

        static TrackHeightfieldComponent* findTrack(World* world){
            if(world == nullptr) return nullptr;
            for(auto entity : world->getEntities()){
                if(auto* track = entity->getComponent<TrackHeightfieldComponent>()) return track;
            }
            return nullptr;
        }

        struct TrackSample {
            bool valid = false;
            float y = 0.0f;
            TrackHeightfieldComponent::SurfaceType surface = TrackHeightfieldComponent::SurfaceType::Road;
        };

        enum class MoveResult { MovedRoad, MovedGrass, BlockedWall, BlockedUnknown };

        static bool sampleTrack(const TrackHeightfieldComponent* track, float x, float z, TrackSample& out){
            if(track == nullptr) return false;
            TrackHeightfieldComponent::SurfaceType surface;
            float y = 0.0f;
            if(!track->sampleSurface(x, z, y, surface)) return false;
            out.valid = true;
            out.y = y;
            out.surface = surface;
            return true;
        }

        static bool snapToTrack(const TrackHeightfieldComponent* track, glm::vec3& position, float clearance){
            TrackSample s;
            if(!sampleTrack(track, position.x, position.z, s)) return false;
            if(s.surface == TrackHeightfieldComponent::SurfaceType::Wall) return false;
            position.y = s.y + clearance;
            return true;
        }

        static MoveResult tryMove(
            const TrackHeightfieldComponent* track,
            glm::vec3& position,
            const glm::vec3& delta,
            float clearance,
            float collisionRadius,
            float wallPushback,
            int wallResolveIterations,
            float maxClimbHeight,
            TrackHeightfieldComponent::SurfaceType& outSurface,
            bool& outTouchedWall,
            glm::vec2& outWallNormal,
            const CarControllerComponent* car = nullptr
        ) {
            outTouchedWall = false;
            outWallNormal = glm::vec2(0.0f);

            auto tryCandidate = [&](glm::vec3 candidate) -> MoveResult {
                bool touchedWall = false;
                glm::vec2 preResolve(candidate.x, candidate.z);
                if(track != nullptr && !(car && car->noClip)){
                    glm::vec2 p(candidate.x, candidate.z);
                    touchedWall = track->resolveWallCollision(p, collisionRadius, wallPushback, wallResolveIterations);
                    if(touchedWall){
                        glm::vec2 pushDir = p - preResolve;
                        float pushLen = glm::length(pushDir);
                        if(pushLen > 1e-6f) outWallNormal = pushDir / pushLen;
                    }
                    candidate.x = p.x;
                    candidate.z = p.y;
                }

                TrackSample s;
                if(!sampleTrack(track, candidate.x, candidate.z, s)){
                    if(track != nullptr && track->containsXZ(candidate.x, candidate.z)){
                        candidate.y = position.y;
                        position.x = candidate.x;
                        position.z = candidate.z;
                        outTouchedWall = touchedWall;
                        outSurface = TrackHeightfieldComponent::SurfaceType::Grass;
                        return MoveResult::MovedGrass;
                    }
                    if(car && car->noClip){
                        candidate.y = position.y;
                        position = candidate;
                        outTouchedWall = touchedWall;
                        outSurface = TrackHeightfieldComponent::SurfaceType::Grass;
                        return MoveResult::MovedGrass;
                    }
                    outTouchedWall = touchedWall;
                    return MoveResult::BlockedUnknown;
                }
                if(s.surface == TrackHeightfieldComponent::SurfaceType::Wall){
                    if(car && car->noClip){
                        if(std::abs(s.y + clearance - position.y) > maxClimbHeight){
                            s.y = position.y - clearance;
                        }
                    } else {
                        outTouchedWall = true;
                        return MoveResult::BlockedWall;
                    }
                }

                const float targetY = s.y + clearance;
                candidate.y = resolveVerticalTarget(position.y, targetY, maxClimbHeight, touchedWall);
                position = candidate;
                outTouchedWall = touchedWall;
                outSurface = s.surface;
                return (s.surface == TrackHeightfieldComponent::SurfaceType::Grass)
                    ? MoveResult::MovedGrass : MoveResult::MovedRoad;
            };

            MoveResult result = tryCandidate(position + delta);
            if(result == MoveResult::MovedRoad || result == MoveResult::MovedGrass) return result;

            result = tryCandidate(position + glm::vec3(delta.x, 0.0f, 0.0f));
            if(result == MoveResult::MovedRoad || result == MoveResult::MovedGrass) return result;

            result = tryCandidate(position + glm::vec3(0.0f, 0.0f, delta.z));
            if(result == MoveResult::MovedRoad || result == MoveResult::MovedGrass) return result;

            return result;
        }

        static void cacheFrontWheelParts(CarControllerComponent& car, const MultiMeshRendererComponent& multi){
            if(car._cachedFrontWheelParts) return;

            struct Candidate { int idx; float score; glm::vec3 p; };
            std::vector<Candidate> candidates;
            candidates.reserve(multi.parts.size());

            const std::vector<std::string> wheelHints = {"wheel", "tire", "tyre", "rim"};
            const std::vector<std::string> wheelExclusions = {"body", "chassis", "frame", "kart", "car", "cockpit", "engine", "steering", "seat"};

            for(int i = 0; i < (int)multi.parts.size(); i++){
                const auto& part = multi.parts[i];
                const std::string fullNameLower = toLowerCopy(part.objectName + std::string(" ") + part.materialName);
                if(containsAnySubstringCI(fullNameLower, wheelExclusions)) continue;

                const glm::vec3 s = glm::abs(part.aabbSize);
                const float a = s.x, b = s.y, c = s.z;
                const float maxDim = std::max(a, std::max(b, c));
                const float minDim = std::min(a, std::min(b, c));
                const float midDim = (a + b + c) - maxDim - minDim;
                if(maxDim <= 1e-6f || midDim <= 1e-6f) continue;

                const float roundness = std::clamp(midDim / maxDim, 0.0f, 1.0f);
                const float thinness = std::clamp(minDim / midDim, 0.0f, 1.0f);

                float nameBonus = 0.0f;
                bool hasWheelName = false;
                for(const auto& h : wheelHints){
                    if(fullNameLower.find(h) != std::string::npos){
                        nameBonus = 1.0f;
                        hasWheelName = true;
                        break;
                    }
                }

                if(!hasWheelName && roundness < 0.75f) continue;
                if(roundness < 0.45f) continue;

                float score = 0.0f;
                score += 2.0f * roundness;
                score += 1.0f * (1.0f - thinness);
                score += nameBonus;
                score += -0.1f * part.localTransform.position.y;

                candidates.push_back({i, score, part.localTransform.position});
            }

            if(candidates.size() < 2){
                car._cachedFrontWheelParts = true;
                return;
            }

            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b){ return a.score > b.score; });
            if(candidates.size() > 8) candidates.resize(8);

            glm::vec3 center(0.0f);
            for(const auto& c : candidates) center += c.p;
            center /= (float)candidates.size();

            std::vector<int> picked;
            picked.reserve(4);
            while(picked.size() < 4 && !candidates.empty()){
                int bestIdx = -1;
                float bestVal = -1e9f;
                for(const auto& c : candidates){
                    if(std::find(picked.begin(), picked.end(), c.idx) != picked.end()) continue;
                    const float dist = glm::length(glm::vec2(c.p.x - center.x, c.p.z - center.z));
                    const float val = dist + 0.15f * c.score;
                    if(val > bestVal){ bestVal = val; bestIdx = c.idx; }
                }
                if(bestIdx < 0) break;
                picked.push_back(bestIdx);
            }

            if(picked.size() < 2){
                car._cachedFrontWheelParts = true;
                return;
            }

            std::vector<std::pair<float,int>> byZ;
            byZ.reserve(picked.size());
            for(int idx : picked){
                byZ.push_back({multi.parts[idx].localTransform.position.z, idx});
            }
            std::sort(byZ.begin(), byZ.end(), [](auto a, auto b){ return a.first > b.first; });

            car._frontWheelPartIndices.clear();
            car._frontWheelBaseYaw.clear();
            const int count = std::min(2, (int)byZ.size());
            for(int i = 0; i < count; i++){
                const int idx = byZ[i].second;
                car._frontWheelPartIndices.push_back(idx);
                car._frontWheelBaseYaw.push_back(multi.parts[idx].localTransform.rotation.y);
            }

            car._cachedFrontWheelParts = true;
        }

    public:
        void enter(Application* application){ app = application; }

        void update(World* world, float deltaTime){
            if(app == nullptr) return;

            const float dt = std::clamp(deltaTime, 0.0f, 0.05f); // Cap at 50ms for stability
            auto* track = findTrack(world);
            auto& keyboard = app->getKeyboard();

            for(auto entity : world->getEntities()){
                auto* car = entity->getComponent<CarControllerComponent>();
                if(car == nullptr) continue;

                auto& transform = entity->localTransform;
                TrackHeightfieldComponent::SurfaceType currentSurface = TrackHeightfieldComponent::SurfaceType::Road;

                // ── 1. Terrain snap at start of frame (existing logic, unchanged) ──
                TrackSample startSample;
                if(sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                   startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall) {
                    const float targetY = startSample.y + car->groundClearance;
                    transform.position.y = resolveVerticalTarget(transform.position.y, targetY, car->maxClimbHeight, false);
                    currentSurface = startSample.surface;
                } else if(track != nullptr) {
                    bool recovered = false;
                    glm::vec2 nudged(transform.position.x, transform.position.z);
                    const bool touchedWallOnRecover = track->resolveWallCollision(
                        nudged, std::max(0.05f, car->collisionRadius), 0.0f, std::max(1, car->wallResolveIterations));
                    if(touchedWallOnRecover){
                        transform.position.x = nudged.x;
                        transform.position.z = nudged.y;
                        if(sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                           startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall){
                            transform.position.y = resolveVerticalTarget(
                                transform.position.y, startSample.y + car->groundClearance, car->maxClimbHeight, true);
                            currentSurface = startSample.surface;
                            recovered = true;
                        }
                    }
                    if(!recovered && !track->containsXZ(transform.position.x, transform.position.z)){
                        const glm::vec2 oldXZ(transform.position.x, transform.position.z);
                        glm::vec2 projected = oldXZ;
                        constexpr int kRecoverCells = 12;
                        if(track->projectToNearestDrivable(projected, kRecoverCells)){
                            const float maxRecoverDistance = std::max(1.0f, car->collisionRadius * 10.0f);
                            if(glm::length(projected - oldXZ) <= maxRecoverDistance){
                                transform.position.x = projected.x;
                                transform.position.z = projected.y;
                                if(sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                                   startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall){
                                    transform.position.y = resolveVerticalTarget(
                                        transform.position.y, startSample.y + car->groundClearance, car->maxClimbHeight, false);
                                    currentSurface = startSample.surface;
                                    recovered = true;
                                }
                            }
                        }
                    }
                    if(!recovered && track->containsXZ(transform.position.x, transform.position.z)){
                        currentSurface = TrackHeightfieldComponent::SurfaceType::Grass;
                    }
                }

                // ── 2. Read input ──
                const bool  pressingForward = keyboard.isPressed(GLFW_KEY_W);
                const bool  pressingReverse = keyboard.isPressed(GLFW_KEY_S);
                const bool  pressingBrake   = keyboard.isPressed(GLFW_KEY_SPACE);
                const float steerInput      = (keyboard.isPressed(GLFW_KEY_A) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_D) ? 1.0f : 0.0f);

                // ── 3. Surface multipliers ──
                const bool onGrass = (currentSurface == TrackHeightfieldComponent::SurfaceType::Grass);
                const float surfaceEngine  = onGrass ? car->grassEngineScale  : 1.0f;
                const float surfaceLateral = onGrass ? car->grassLateralScale : 1.0f;
                const float surfaceDrag    = onGrass ? car->grassDragScale    : 1.0f;
                const float surfaceMaxSpd  = onGrass ? car->grassMaxSpeedScale : 1.0f;
                const float effectiveMaxSpeed = car->maxSpeed * surfaceMaxSpd;

                // ── 4. Decompose velocity into local frame ──
                const glm::vec3 fwd3 = getForward(transform.rotation.y);
                const glm::vec3 rgt3 = getRight(transform.rotation.y);
                const glm::vec2 fwdDir(fwd3.x, fwd3.z);
                const glm::vec2 rgtDir(rgt3.x, rgt3.z);

                float fwdVel = glm::dot(car->velocity, fwdDir);
                float latVel = glm::dot(car->velocity, rgtDir);

                // ── 5. Compute slip angle & grip ──
                const float absFwd = std::abs(fwdVel);
                const float slipAngle = std::atan2(std::abs(latVel), absFwd + 0.1f);

                float gripFactor = 1.0f;
                if(slipAngle > car->maxGripSlipAngle){
                    float t = std::clamp(
                        (slipAngle - car->maxGripSlipAngle) / (car->driftSlipAngle - car->maxGripSlipAngle),
                        0.0f, 1.0f);
                    gripFactor = 1.0f - t * (1.0f - car->driftGripFactor);
                }
                const bool isDrifting = (slipAngle > car->driftSlipAngle * 0.8f && absFwd > 2.0f);

                // ── 6. Apply forces in local space ──

                // Engine / brake
                if(pressingForward){
                    if(fwdVel < effectiveMaxSpeed){
                        fwdVel += car->engineForce * surfaceEngine * dt;
                    }
                }

                // Braking (Space or S while moving forward)
                const bool isBraking = pressingBrake || (pressingReverse && fwdVel > 0.2f);
                if(isBraking){
                    fwdVel -= car->brakeForce * dt;
                    // Don't let braking alone push you into reverse
                    if(fwdVel < 0.0f && !pressingReverse) fwdVel = 0.0f;
                }

                // Reversing (S only after stopping)
                if(pressingReverse && fwdVel <= 0.2f){
                    if(fwdVel > -car->maxReverseSpeed * surfaceMaxSpd){
                        fwdVel -= car->engineForce * surfaceEngine * 0.5f * dt;
                    }
                }

                // Rolling resistance (velocity-proportional drag)
                {
                    const float drag = car->rollingResistance * surfaceDrag * dt;
                    if(std::abs(fwdVel) > 0.01f){
                        float reduction = fwdVel * std::clamp(drag, 0.0f, 0.95f);
                        fwdVel -= reduction;
                    }
                }

                // Aerodynamic drag (proportional to v²)
                {
                    float aeroDrag = car->aeroDragCoeff * fwdVel * std::abs(fwdVel) * dt;
                    // Don't let aero drag reverse direction
                    if(std::abs(aeroDrag) > std::abs(fwdVel)) aeroDrag = fwdVel;
                    fwdVel -= aeroDrag;
                }

                // Lateral friction (with grip factor from slip model)
                {
                    float effectiveLatFriction = car->lateralFriction * gripFactor * surfaceLateral;
                    
                    // INDUCE SKID: If braking while moving fast, reduce lateral grip significantly
                    if(isBraking && absFwd > 4.0f){
                        effectiveLatFriction *= 0.10f; 
                    }

                    float latFrictionAmount = effectiveLatFriction * dt;
                    latFrictionAmount = std::clamp(latFrictionAmount, 0.0f, 0.98f);
                    latVel *= (1.0f - latFrictionAmount);
                }

                // Speed clamp
                fwdVel = std::clamp(fwdVel, -car->maxReverseSpeed * surfaceMaxSpd, effectiveMaxSpeed);

                // Clamp lateral to prevent runaway
                const float maxLateral = std::max(effectiveMaxSpeed * 0.6f, 3.0f);
                latVel = std::clamp(latVel, -maxLateral, maxLateral);

                // ── 7. Reconstruct world velocity ──
                car->velocity = fwdDir * fwdVel + rgtDir * latVel;

                // Clamp total velocity magnitude
                {
                    float totalSpeed = glm::length(car->velocity);
                    const float maxTotalSpeed = std::max(effectiveMaxSpeed, car->maxReverseSpeed) * 1.2f;
                    if(totalSpeed > maxTotalSpeed){
                        car->velocity *= (maxTotalSpeed / totalSpeed);
                    }
                }

                // ── 8. Steering ──
                // Smooth steering angle toward input
                {
                    float targetSteer = steerInput * car->steerAngleMax;

                    // Reduce steering at high speed
                    float speedRatio = std::clamp(absFwd / std::max(1.0f, effectiveMaxSpeed), 0.0f, 1.0f);
                    float steerScale = 1.0f - speedRatio * (1.0f - car->highSpeedSteerReduction);
                    targetSteer *= steerScale;

                    if(std::abs(steerInput) > 0.01f){
                        float diff = targetSteer - car->currentSteerAngle;
                        float step = car->steerSpeed * dt;
                        if(std::abs(diff) < step) car->currentSteerAngle = targetSteer;
                        else car->currentSteerAngle += (diff > 0.0f ? step : -step);
                    } else {
                        float step = car->steerReturnSpeed * dt;
                        if(std::abs(car->currentSteerAngle) < step) car->currentSteerAngle = 0.0f;
                        else car->currentSteerAngle -= (car->currentSteerAngle > 0.0f ? step : -step);
                    }
                }

                // ── 9. Yaw rate from steering (bicycle model) ──
                {
                    float steerFwd = fwdVel;
                    // Reverse steering when going backward
                    if(fwdVel < -0.1f) steerFwd = fwdVel;

                    if(std::abs(steerFwd) > car->minSteerSpeed){
                        float tanSteer = std::tan(car->currentSteerAngle);
                        float desiredYawRate = steerFwd * tanSteer / car->wheelbase;
                        // Smooth toward desired
                        float blendRate = 8.0f * dt;
                        blendRate = std::clamp(blendRate, 0.0f, 1.0f);
                        car->yawRate += (desiredYawRate - car->yawRate) * blendRate;
                    } else {
                        // Nearly stopped: damp yaw to zero
                        car->yawRate *= std::max(0.0f, 1.0f - 10.0f * dt);
                    }

                    // Drift assist: extra angular damping when drifting to prevent spinout
                    if(isDrifting){
                        car->yawRate *= std::max(0.0f, 1.0f - car->driftAssist * dt);
                    }

                    // Angular damping
                    car->yawRate *= std::max(0.0f, 1.0f - car->angularDamping * dt);

                    // Clamp yaw rate
                    car->yawRate = std::clamp(car->yawRate, -car->maxYawRate, car->maxYawRate);

                    // Integrate yaw
                    transform.rotation.y += car->yawRate * dt;
                }

                // ── 10. Position integration with sub-stepped collision ──
                const glm::vec2 frameDelta = car->velocity * dt;
                const float totalDistance = glm::length(frameDelta);
                const float subStepDist = std::max(0.05f, car->collisionSubstepDistance);
                const int subSteps = std::clamp((int)std::ceil(totalDistance / subStepDist), 1, 16);
                const glm::vec2 stepDelta2D = frameDelta / (float)subSteps;
                const glm::vec3 stepDelta(stepDelta2D.x, 0.0f, stepDelta2D.y);

                auto applyWallBounce = [&](const glm::vec2& wallNormal){
                    float impactSpeed = glm::length(car->velocity);
                    if(impactSpeed < car->wallBounceMinSpeed){
                        car->velocity = glm::vec2(0.0f);
                        car->yawRate *= 0.3f;
                        return;
                    }
                    if(glm::length(wallNormal) > 0.5f){
                        // Reflect velocity across wall normal
                        glm::vec2 n = glm::normalize(wallNormal);
                        float vn = glm::dot(car->velocity, n);
                        if(vn < 0.0f){
                            car->velocity -= 2.0f * vn * n;
                        }
                    } else {
                        // No good normal: just reverse
                        car->velocity = -car->velocity;
                    }
                    car->velocity *= car->wallBounceDamping;
                    car->yawRate *= 0.5f;
                };

                for(int i = 0; i < subSteps; i++){
                    TrackHeightfieldComponent::SurfaceType steppedSurface = currentSurface;
                    bool touchedWall = false;
                    glm::vec2 wallNormal(0.0f);
                    const MoveResult move = tryMove(
                        track, transform.position, stepDelta,
                        car->groundClearance, car->collisionRadius,
                        car->wallPushback, car->wallResolveIterations,
                        car->maxClimbHeight, steppedSurface, touchedWall,
                        wallNormal, car);

                    if(move == MoveResult::MovedGrass || move == MoveResult::MovedRoad){
                        currentSurface = steppedSurface;
                        if(touchedWall){
                            applyWallBounce(wallNormal);
                            break;
                        }
                        continue;
                    }

                    if(move == MoveResult::BlockedWall || touchedWall){
                        applyWallBounce(wallNormal);
                    } else {
                        car->velocity *= 0.9f;
                    }
                    break;
                }

                // ── 11. Update backward-compatible speed (for WheelSpin & HUD) ──
                {
                    const glm::vec3 fwdNow = getForward(transform.rotation.y);
                    const glm::vec2 fwdDirNow(fwdNow.x, fwdNow.z);
                    car->speed = glm::dot(car->velocity, fwdDirNow);
                }

                // ── 12. Visual wheel steering angle ──
                car->steeringAngle = car->currentSteerAngle;

                if(auto* multi = entity->getComponent<MultiMeshRendererComponent>()){
                    cacheFrontWheelParts(*car, *multi);
                    for(size_t i = 0; i < car->_frontWheelPartIndices.size(); i++){
                        const int idx = car->_frontWheelPartIndices[i];
                        if(idx < 0 || idx >= (int)multi->parts.size()) continue;
                        multi->parts[idx].localTransform.rotation.y = car->_frontWheelBaseYaw[i] + car->steeringAngle;
                    }
                }
            }
        }
    };

}
