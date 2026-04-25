#pragma once

#include "../ecs/component.hpp"

#include <string>

namespace our {

    // Attached to every racer (player or AI) to track their progress through
    // the checkpoint sequence.  The RaceSystem reads and writes these fields
    // each frame.
    class RaceProgressComponent : public Component {
    public:
        int currentLap = 0;            // How many full laps the racer has completed
        int nextCheckpointIndex = 0;   // Index of the next checkpoint the racer must reach
        int totalLaps = 3;             // Number of laps required to finish the race

        static std::string getID() { return "Race Progress"; }

        void deserialize(const nlohmann::json& data) override;
    };

}
