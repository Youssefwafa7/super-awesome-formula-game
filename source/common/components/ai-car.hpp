#pragma once

#include "../ecs/component.hpp"

#include <string>

namespace our
{

  class AICarComponent : public Component
  {
  public:
    static std::string getID() { return "AI Car"; }

    float desiredSpeed = 14.0f;
    float waypointReachDistance = 2.2f;
    int lookAheadPoints = 6;
    float steerResponsiveness = 1.0f;
    float laneOffset = 0.0f;

    // Runtime state
    bool _initialized = false;
    int _currentWaypoint = 0;

    void deserialize(const nlohmann::json &data) override;
  };

}
