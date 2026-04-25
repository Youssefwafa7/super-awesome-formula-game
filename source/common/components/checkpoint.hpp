#pragma once

#include "../ecs/component.hpp"

#include <glm/glm.hpp>

#include <string>

namespace our {

    // Marks an entity as a checkpoint in the race track.
    // The RaceSystem uses entities with this component to define the ordered
    // sequence of checkpoints that racers must pass through each lap.
    class CheckpointComponent : public Component {
    public:
        int index = 0;           // Sequence index of this checkpoint (0-based, lap order)
        float radius = 5.0f;     // Detection radius – a racer is "inside" when closer than this

        static std::string getID() { return "Checkpoint"; }

        void deserialize(const nlohmann::json& data) override;
    };

}
