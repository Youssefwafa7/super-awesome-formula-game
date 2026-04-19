#include "ai-car.hpp"

#include <algorithm>

namespace our
{

  void AICarComponent::deserialize(const nlohmann::json &data)
  {
    if (!data.is_object())
      return;

    desiredSpeed = data.value("desiredSpeed", desiredSpeed);
    waypointReachDistance = data.value("waypointReachDistance", waypointReachDistance);
    lookAheadPoints = data.value("lookAheadPoints", lookAheadPoints);
    steerResponsiveness = data.value("steerResponsiveness", steerResponsiveness);
    laneOffset = data.value("laneOffset", laneOffset);

    desiredSpeed = std::max(3.0f, desiredSpeed);
    waypointReachDistance = std::clamp(waypointReachDistance, 0.5f, 8.0f);
    lookAheadPoints = std::clamp(lookAheadPoints, 1, 24);
    steerResponsiveness = std::clamp(steerResponsiveness, 0.4f, 3.0f);
    laneOffset = std::clamp(laneOffset, -2.0f, 2.0f);
  }

}
