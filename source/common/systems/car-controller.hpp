#pragma once

#include "../application.hpp"
#include "../ecs/world.hpp"
#include "../components/car-controller.hpp"
#include "../components/track-heightfield.hpp"
#include "../components/multi-mesh-renderer.hpp"

#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace our {

    // Updates entities with CarControllerComponent using keyboard input,
    // and constrains them to a TrackHeightfieldComponent.
    //
    // Controls:
    // - W/S: forward/reverse
    // - A/D: steering
    class CarControllerSystem {
        Application* app = nullptr;

        static float resolveVerticalTarget(
            float currentY,
            float sampledTargetY,
            float maxClimbHeight,
            bool touchedWall
        ) {
            // Never allow wall contact to turn into upward "wall climbing".
            if(sampledTargetY > currentY && touchedWall) return currentY;

            // Always allow moving down to follow terrain.
            if(sampledTargetY <= currentY) return sampledTargetY;

            // Limit upward steps even when not touching walls.
            if(sampledTargetY > currentY + maxClimbHeight) return currentY;
            return sampledTargetY;
        }

        static glm::vec3 getForward(float yaw){
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            // Many imported car OBJs face +Z in their local space.
            return glm::vec3(rot * glm::vec4(0, 0, 1, 0));
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

        enum class MoveResult {
            MovedRoad,
            MovedGrass,
            BlockedWall,
            BlockedUnknown
        };

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
            const CarControllerComponent* car = nullptr
        ) {
            outTouchedWall = false;

            auto tryCandidate = [&](glm::vec3 candidate) -> MoveResult {
                bool touchedWall = false;
                if(track != nullptr && !(car && car->noClip)){
                    glm::vec2 p(candidate.x, candidate.z);
                    touchedWall = track->resolveWallCollision(p, collisionRadius, wallPushback, wallResolveIterations);
                    candidate.x = p.x;
                    candidate.z = p.y;
                }

                TrackSample s;
                if(!sampleTrack(track, candidate.x, candidate.z, s)){
                    // If still inside the track bounds but no classified cell exists,
                    // treat this as soft grass (keep current vertical position).
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
                        // In no-clip mode, treat walls as road/grass height-wise but don't block motion.
                        // If the wall has no drivable surface underneath (s.y is 0 or too far), 
                        // keep our current height to avoid snapping to the floor/void.
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
                    ? MoveResult::MovedGrass
                    : MoveResult::MovedRoad;
            };

            MoveResult result = tryCandidate(position + delta);
            if(result == MoveResult::MovedRoad || result == MoveResult::MovedGrass) return result;

            // Slide: try X-only then Z-only.
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

                // Geometry heuristic: wheels are often round in two axes and thin in the third.
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
                // Prefer parts near ground.
                score += -0.1f * part.localTransform.position.y;

                candidates.push_back({i, score, part.localTransform.position});
            }

            if(candidates.size() < 2){
                car._cachedFrontWheelParts = true;
                return;
            }

            // Pick up to 32 best wheel candidates to ensure we see the whole car.
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b){ return a.score > b.score; });
            if(candidates.size() > 32) candidates.resize(32);

            // Compute center of all candidates to help distinguish corners.
            glm::vec3 center(0.0f);
            for(const auto& c : candidates) center += c.p;
            center /= (float)candidates.size();

            // 1. Identify the 4 distinct corners by picking the 4 most spread-out wheel parts.
            std::vector<int> cornerAnchors;
            cornerAnchors.reserve(4);
            while(cornerAnchors.size() < 4 && !candidates.empty()){
                int bestIdx = -1;
                float bestVal = -1e9f;
                for(const auto& c : candidates){
                    bool alreadyPicked = false;
                    for(int anchor : cornerAnchors) if(anchor == c.idx) alreadyPicked = true;
                    if(alreadyPicked) continue;

                    float minDist = glm::length(glm::vec2(c.p.x - center.x, c.p.z - center.z));
                    for(int anchorIdx : cornerAnchors){
                        const glm::vec3& anchorP = multi.parts[anchorIdx].localTransform.position;
                        minDist = std::min(minDist, glm::length(glm::vec2(c.p.x - anchorP.x, c.p.z - anchorP.z)));
                    }
                    const float val = minDist + 0.1f * c.score;
                    if(val > bestVal){ bestVal = val; bestIdx = c.idx; }
                }
                if(bestIdx < 0) break;
                cornerAnchors.push_back(bestIdx);
            }

            // 2. Identify which of these corners are "Front" (highest Z).
            std::vector<int> frontAnchors;
            if(cornerAnchors.size() >= 2){
                std::sort(cornerAnchors.begin(), cornerAnchors.end(), [&](int a, int b){
                    return multi.parts[a].localTransform.position.z > multi.parts[b].localTransform.position.z;
                });
                // We assume the top 2 unique Z positions (or just top 2 parts if Zs are same) are front.
                frontAnchors.push_back(cornerAnchors[0]);
                frontAnchors.push_back(cornerAnchors[1]);
                // If there are more than 2 corners and the 3rd is very close in Z to the 2nd, include it (for 6-wheelers).
                if(cornerAnchors.size() > 2){
                    float z2 = multi.parts[cornerAnchors[1]].localTransform.position.z;
                    float z3 = multi.parts[cornerAnchors[2]].localTransform.position.z;
                    if(std::abs(z2 - z3) < 0.5f) frontAnchors.push_back(cornerAnchors[2]);
                }
            }

            // 3. For each front corner, collect ALL parts in 'candidates' that are near it.
            car._frontWheelPartIndices.clear();
            car._frontWheelBaseYaw.clear();
            for(const auto& c : candidates){
                // Find which of the 4 corner anchors this part is closest to
                int closestAnchor = -1;
                float minDist = 1e9f;
                for(int anchorIdx : cornerAnchors){
                    const glm::vec3& anchorP = multi.parts[anchorIdx].localTransform.position;
                    float d = glm::length(glm::vec2(c.p.x - anchorP.x, c.p.z - anchorP.z));
                    if(d < minDist){
                        minDist = d;
                        closestAnchor = anchorIdx;
                    }
                }
                
                // If the closest anchor is one of our front anchors, this part belongs to a front wheel!
                bool isFront = false;
                for(int fa : frontAnchors) if(fa == closestAnchor) isFront = true;
                
                if(isFront){
                    bool alreadyIn = false;
                    for(int existing : car._frontWheelPartIndices) if(existing == c.idx) alreadyIn = true;
                    if(!alreadyIn){
                        car._frontWheelPartIndices.push_back(c.idx);
                        car._frontWheelBaseYaw.push_back(multi.parts[c.idx].localTransform.rotation.y);
                    }
                }
            }

            car._cachedFrontWheelParts = true;
        }

        // Compute the car's pitch from the drivable surface triangle directly
        // beneath it. Pitch only (no roll). Uses simple lerp for smooth blending.
        static void applySlopeAlignment(
            const TrackHeightfieldComponent* track,
            CarControllerComponent* car,
            Transform& transform,
            float deltaTime
        ) {
            if(track == nullptr || car == nullptr) return;

            float surfY;
            glm::vec3 surfNormal;
            const bool hasSurface = track->sampleDrivableSurface(
                transform.position.x, transform.position.z, surfY, surfNormal
            );

            float targetPitch = 0.0f; // default: flat

            if(hasSurface){
                // Ensure normal points upward
                if(surfNormal.y < 0.0f) surfNormal = -surfNormal;
                const float ny = surfNormal.y;

                // Only compute pitch if the normal isn't perfectly vertical
                if(ny > 1e-4f && ny < 0.9999f){
                    // Get the car's XZ forward direction from its yaw
                    const glm::vec3 fwd = getForward(transform.rotation.y);

                    // The slope along the forward direction:
                    // slope = -(nx*fx + nz*fz) / ny
                    // pitch = atan(slope)
                    const float slopeAlongForward = -(surfNormal.x * fwd.x + surfNormal.z * fwd.z) / ny;
                    targetPitch = -std::atan(slopeAlongForward);
                }

                // Clamp
                targetPitch = std::clamp(targetPitch, -car->maxPitchAngle, car->maxPitchAngle);
            }

            // Smooth toward target pitch (frame-rate independent exponential lerp)
            const float alpha = 1.0f - std::exp(-car->slopeSmoothingSpeed * deltaTime);
            const float currentPitch = transform.rotation.x;
            transform.rotation.x = currentPitch + (targetPitch - currentPitch) * alpha;

            // No roll
            transform.rotation.z = 0.0f;
        }

    public:
        void enter(Application* application){ app = application; }

        void update(World* world, float deltaTime, bool isRaceStarted = true){
            if(app == nullptr) return;

            auto* track = findTrack(world);

            auto& keyboard = app->getKeyboard();

            for(auto entity : world->getEntities()){
                auto* car = entity->getComponent<CarControllerComponent>();
                if(car == nullptr) continue;

                auto& transform = entity->localTransform;

                TrackHeightfieldComponent::SurfaceType currentSurface = TrackHeightfieldComponent::SurfaceType::Road;

                // Keep the car vertically attached to the sampled surface at start of frame.
                TrackSample startSample;
                if(sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                   startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall) {
                    const float targetY = startSample.y + car->groundClearance;
                    transform.position.y = resolveVerticalTarget(
                        transform.position.y,
                        targetY,
                        car->maxClimbHeight,
                        false
                    );
                    currentSurface = startSample.surface;
                } else if(track != nullptr) {
                    bool recovered = false;

                    // First, try a local depenetration from wall segments only.
                    // This avoids large snap corrections when skimming walls or grass edges.
                    glm::vec2 nudged(transform.position.x, transform.position.z);
                    const bool touchedWallOnRecover = track->resolveWallCollision(
                        nudged,
                        std::max(0.05f, car->collisionRadius),
                        0.0f,
                        std::max(1, car->wallResolveIterations)
                    );
                    if(touchedWallOnRecover){
                        transform.position.x = nudged.x;
                        transform.position.z = nudged.y;
                        if(sampleTrack(track, transform.position.x, transform.position.z, startSample) &&
                           startSample.surface != TrackHeightfieldComponent::SurfaceType::Wall){
                            const float targetY = startSample.y + car->groundClearance;
                            transform.position.y = resolveVerticalTarget(
                                transform.position.y,
                                targetY,
                                car->maxClimbHeight,
                                true
                            );
                            currentSurface = startSample.surface;
                            recovered = true;
                        }
                    }

                    // If outside bounds, allow projection but keep it local to prevent teleporting.
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
                                    const float targetY = startSample.y + car->groundClearance;
                                    transform.position.y = resolveVerticalTarget(
                                        transform.position.y,
                                        targetY,
                                        car->maxClimbHeight,
                                        false
                                    );
                                    currentSurface = startSample.surface;
                                    recovered = true;
                                }
                            }
                        }
                    }

                    // If still unresolved but still within track bounds, keep motion continuous.
                    if(!recovered && track->containsXZ(transform.position.x, transform.position.z)){
                        currentSurface = TrackHeightfieldComponent::SurfaceType::Grass;
                    }
                }

                float throttle = 0.0f;
                float steer = 0.0f;
                if(isRaceStarted){
                    throttle = (keyboard.isPressed(GLFW_KEY_W) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_S) ? 1.0f : 0.0f);
                    steer = (keyboard.isPressed(GLFW_KEY_A) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_D) ? 1.0f : 0.0f);

                    GLFWgamepadstate state;
                    if (glfwGetGamepadState(GLFW_JOYSTICK_1, &state)) {
                        float rt = (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) / 2.0f;
                        float lt = (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) / 2.0f;
                        float leftX = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
                        float rb = state.buttons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] == GLFW_PRESS ? 1.0f : 0.0f;
                        float lb = state.buttons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] == GLFW_PRESS ? 1.0f : 0.0f;

                        // Deadzone for analog stick
                        if (std::abs(leftX) < 0.1f) leftX = 0.0f;

                        throttle += (rt - lt) + (rb - lb);
                        steer += -leftX; // Left is positive, Right is negative in this engine

                        throttle = std::clamp(throttle, -1.0f, 1.0f);
                        steer = std::clamp(steer, -1.0f, 1.0f);
                    }
                }
                
                const bool onGrass = (currentSurface == TrackHeightfieldComponent::SurfaceType::Grass);
                const float accelFactor = onGrass ? car->grassAccelFactor : 1.0f;

                // Tick reverse cooldown timer
                if(car->reverseCooldownTimer > 0.0f){
                    car->reverseCooldownTimer -= deltaTime;
                }

                // Update speed.
                if(throttle > 0.0f){
                    if(car->speed < 0.0f){
                        // Braking while moving backward
                        car->speed += (car->brakeAcceleration * 0.6f) * accelFactor * deltaTime;
                        if(car->speed > 0.0f) car->speed = 0.0f;
                    } else {
                        // Accelerating forward
                        car->speed += car->acceleration * accelFactor * deltaTime;
                    }
                } else if(throttle < 0.0f){
                    if(car->speed > 0.0f){
                        // Braking while moving forward
                        car->speed -= (car->brakeAcceleration * 0.5f) * accelFactor * deltaTime;
                        if(car->speed <= 0.0f) {
                            car->speed = 0.0f;
                            car->reverseCooldownTimer = 0.5f; // 0.5 seconds cooldown
                        }
                    } else {
                        // Accelerating backward (Reverse)
                        if(car->reverseCooldownTimer <= 0.0f){
                            car->speed -= car->acceleration * accelFactor * deltaTime;
                        }
                    }
                } else {
                    // Coasting (No throttle, no brake)
                    // Apply momentum/inertia: slow stop
                    const float extraGrassDrag = onGrass ? (0.35f * car->grassDamping) : 0.0f;
                    const float rollingResistance = (car->acceleration * 0.27f) + (onGrass ? (2.0f * car->grassDamping) : 0.0f);
                    
                    if(car->speed > 0.0f){
                        car->speed -= rollingResistance * deltaTime;
                        if(car->speed < 0.0f) car->speed = 0.0f;
                    } else if(car->speed < 0.0f){
                        car->speed += rollingResistance * deltaTime;
                        if(car->speed > 0.0f) car->speed = 0.0f;
                    }
                }

                // Clamp to base limits first to avoid runaway speed.
                car->speed = std::clamp(car->speed, -car->maxReverseSpeed, car->maxSpeed);

                // On grass, bleed excess speed smoothly toward a lower effective max.
                if(onGrass){
                    const float grassForwardLimit = std::max(0.5f, car->maxSpeed * car->grassSpeedFactor);
                    const float grassReverseLimit = std::max(0.35f, car->maxReverseSpeed * car->grassSpeedFactor);
                    const float bleed = std::clamp(car->grassDamping * deltaTime, 0.0f, 1.0f);

                    if(car->speed > grassForwardLimit){
                        car->speed -= (car->speed - grassForwardLimit) * bleed;
                    } else if(car->speed < -grassReverseLimit){
                        car->speed += ((-grassReverseLimit) - car->speed) * bleed;
                    }
                }

                // Turning. (Less turning when nearly stopped.)
                const float speedRatio = std::clamp(std::abs(car->speed) / std::max(1e-3f, car->maxSpeed), 0.0f, 1.0f);
                const float angleFactor = 1.0f - 0.44f * speedRatio; // Steering angle decreases with speed
                const float turnFactor = speedRatio * angleFactor; // Turning rate proportional to speed
                const float grassTurnFactor = onGrass ? car->grassTurnFactor : 1.0f;
                
                if(std::abs(car->speed) > 0.05f){
                    // Swap steering while reversing.
                    const float reversing = (car->speed < -0.1f) ? -1.0f : 1.0f;
                    transform.rotation.y += (steer * reversing) * car->turnSpeed * deltaTime * turnFactor * grassTurnFactor;
                }

                // Integrate position in XZ using sub-steps for stable collision near walls.
                const glm::vec3 forward = getForward(transform.rotation.y);
                const float totalDistance = std::abs(car->speed * deltaTime);
                const float subStepDistance = std::max(0.05f, car->collisionSubstepDistance);
                const int subSteps = std::clamp((int)std::ceil(totalDistance / subStepDistance), 1, 12);
                const glm::vec3 stepDelta = forward * ((car->speed * deltaTime) / (float)subSteps);

                auto applyWallBounce = [&](){
                    const float rebound = std::clamp(car->wallBounceDamping, 0.0f, 1.0f);
                    const float minImpactSpeed = 0.12f;
                    if(std::abs(car->speed) > minImpactSpeed){
                        car->speed = -car->speed * rebound;
                    } else {
                        car->speed = 0.0f;
                    }
                };

                for(int i = 0; i < subSteps; i++){
                    TrackHeightfieldComponent::SurfaceType steppedSurface = currentSurface;
                    bool touchedWall = false;
                    const MoveResult move = tryMove(
                        track,
                        transform.position,
                        glm::vec3(stepDelta.x, 0.0f, stepDelta.z),
                        car->groundClearance,
                        car->collisionRadius,
                        car->wallPushback,
                        car->wallResolveIterations,
                        car->maxClimbHeight,
                        steppedSurface,
                        touchedWall,
                        car
                    );

                    if(move == MoveResult::MovedGrass || move == MoveResult::MovedRoad){
                        currentSurface = steppedSurface;

                        // Most wall impacts are resolved by depenetration and still count as "moved".
                        // Apply rebound on contact so hits feel physical instead of sticking/sliding only.
                        if(touchedWall){
                            applyWallBounce();
                            break;
                        }
                        continue;
                    }

                    if(move == MoveResult::BlockedWall || touchedWall){
                        applyWallBounce();
                    } else {
                        car->speed *= 0.9f;
                    }
                    break;
                }

                // Visual wheel steering angle (independent of movement).
                float speedRatioVisual = std::clamp(std::abs(car->speed) / std::max(1e-3f, car->maxSpeed), 0.0f, 1.0f);
                float angleFactorVisual = 1.0f - 0.7f * speedRatioVisual;
                car->steeringAngle = steer * glm::radians(car->wheelSteerMaxAngle) * angleFactorVisual;

                // Surface alignment: pitch and roll to match drivable surface normal.
                applySlopeAlignment(track, car, transform, deltaTime);

                // Front wheel steering animation (if MultiMeshRenderer exists).
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
