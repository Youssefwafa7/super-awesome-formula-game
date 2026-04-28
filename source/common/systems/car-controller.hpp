#pragma once

#include "../application.hpp"
#include "../ecs/world.hpp"
#include "../components/car-controller.hpp"
#include "../components/track-heightfield.hpp"


#include <glm/gtx/euler_angles.hpp>

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



    public:
        void enter(Application* application){ app = application; }

        void update(World* world, float deltaTime){
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

                const float throttle = (keyboard.isPressed(GLFW_KEY_W) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_S) ? 1.0f : 0.0f);
                const float steer = (keyboard.isPressed(GLFW_KEY_A) ? 1.0f : 0.0f) - (keyboard.isPressed(GLFW_KEY_D) ? 1.0f : 0.0f);
                
                const bool onGrass = (currentSurface == TrackHeightfieldComponent::SurfaceType::Grass);
                const float accelFactor = onGrass ? car->grassAccelFactor : 1.0f;

                // Update speed.
                if(throttle > 0.0f){
                    car->speed += car->acceleration * accelFactor * deltaTime;
                } else if(throttle < 0.0f){
                    car->speed -= car->brakeAcceleration * accelFactor * deltaTime;
                } else {
                    // Damping towards 0.
                    const float extraGrassDrag = onGrass ? (0.35f * car->grassDamping) : 0.0f;
                    const float damping = std::max(0.0f, 1.0f - (car->linearDamping + extraGrassDrag) * deltaTime);
                    car->speed *= damping;
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
                const float speedFactor = std::clamp(std::abs(car->speed) / std::max(1e-3f, car->maxSpeed), 0.0f, 1.0f);
                const float grassTurnFactor = onGrass ? car->grassTurnFactor : 1.0f;
                // If the car is basically stationary, don't rotate in place (only steer tires visually).
                if(std::abs(car->speed) > 0.05f){
                    // Swap steering while reversing.
                    const float reversing = (car->speed < -0.1f) ? -1.0f : 1.0f;
                    transform.rotation.y += (steer * reversing) * car->turnSpeed * deltaTime * speedFactor * grassTurnFactor;
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
                car->steeringAngle = steer * glm::radians(car->wheelSteerMaxAngle);
            }
        }
    };

}
