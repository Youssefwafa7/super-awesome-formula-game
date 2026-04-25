#include "race-progress.hpp"

namespace our {

    void RaceProgressComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        currentLap          = data.value("currentLap", currentLap);
        nextCheckpointIndex = data.value("nextCheckpointIndex", nextCheckpointIndex);
        totalLaps           = data.value("totalLaps", totalLaps);
    }

}
