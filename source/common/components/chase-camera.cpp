#include "chase-camera.hpp"

#include "../deserialize-utils.hpp"

namespace our {

    void ChaseCameraComponent::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        targetName = data.value("target", targetName);
        offset = data.value("offset", offset);
        lockRotation = data.value("lockRotation", lockRotation);
        pitch = data.value("pitch", pitch);
        if(data.contains("lookAtOffset")){
            lookAtOffset = data.value("lookAtOffset", lookAtOffset);
        } else {
            lookAtHeight = data.value("lookAtHeight", lookAtHeight);
            lookAtOffset = glm::vec3(0.0f, lookAtHeight, 0.0f);
        }
    }

}
