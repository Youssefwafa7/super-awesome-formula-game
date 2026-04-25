#pragma once

#include "../ecs/world.hpp"
#include "../components/car-controller.hpp"
#include "../components/checkpoint.hpp"
#include "../components/race-progress.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <vector>

namespace our {

    // The RaceSystem is responsible for tracking each racer's progress
    // through the checkpoint sequence and incrementing their lap count.
    //
    // Every frame it:
    //   1. Collects all checkpoint entities and sorts them by index.
    //   2. For every entity that has both a CarControllerComponent and a
    //      RaceProgressComponent, it checks the XZ distance to the racer's
    //      next target checkpoint.
    //   3. If the racer is within the checkpoint's detection radius the
    //      target index is advanced.  When the last checkpoint is reached
    //      the lap counter increments and the target resets to 0.
    class RaceSystem {
    public:

        void update(World* world, float /*deltaTime*/) {
            if(world == nullptr) return;

            // ---- Gather and sort checkpoints by index ----
            struct CPInfo {
                int index;
                float radius;
                glm::vec3 position;
            };
            std::vector<CPInfo> checkpoints;

            for(auto entity : world->getEntities()){
                auto* cp = entity->getComponent<CheckpointComponent>();
                if(cp == nullptr) continue;

                CPInfo info;
                info.index    = cp->index;
                info.radius   = cp->radius;
                info.position = entity->localTransform.position;
                checkpoints.push_back(info);
            }

            if(checkpoints.empty()) return;

            // Sort so we can index directly and also know the total count.
            std::sort(checkpoints.begin(), checkpoints.end(),
                [](const CPInfo& a, const CPInfo& b){ return a.index < b.index; });

            const int totalCheckpoints = (int)checkpoints.size();

            // ---- Update every racer ----
            for(auto entity : world->getEntities()){
                auto* car      = entity->getComponent<CarControllerComponent>();
                auto* progress = entity->getComponent<RaceProgressComponent>();
                if(car == nullptr || progress == nullptr) continue;

                // Clamp the target index so it's always valid.
                if(progress->nextCheckpointIndex < 0 ||
                   progress->nextCheckpointIndex >= totalCheckpoints){
                    progress->nextCheckpointIndex = 0;
                }

                const CPInfo& target = checkpoints[progress->nextCheckpointIndex];

                // Distance check in XZ plane (ignore height differences).
                const glm::vec3& racerPos = entity->localTransform.position;
                const float dx = racerPos.x - target.position.x;
                const float dz = racerPos.z - target.position.z;
                const float distSq = dx * dx + dz * dz;

                if(distSq <= target.radius * target.radius){
                    // Racer reached this checkpoint – advance to the next one.
                    progress->nextCheckpointIndex++;

                    if(progress->nextCheckpointIndex >= totalCheckpoints){
                        // Completed all checkpoints – that's one lap.
                        progress->nextCheckpointIndex = 0;
                        progress->currentLap++;
                    }
                }
            }
        }
    };

}
