#include "checkpoint.hpp"

namespace our {

    void CheckpointComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        index  = data.value("index", index);
        radius = data.value("radius", radius);
    }

}
