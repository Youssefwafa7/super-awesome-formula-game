#pragma once

#include "../ecs/entity.hpp"
#include "camera.hpp"
#include "mesh-renderer.hpp"
#include "multi-mesh-renderer.hpp"
#include "chase-camera.hpp"
#include "track-heightfield.hpp"
#include "car-controller.hpp"
#include "free-camera-controller.hpp"
#include "movement.hpp"
#include "wheel-spin.hpp"

namespace our {

    // Given a json object, this function picks and creates a component in the given entity
    // based on the "type" specified in the json object which is later deserialized from the rest of the json object
    inline void deserializeComponent(const nlohmann::json& data, Entity* entity){
        std::string type = data.value("type", "");
        Component* component = nullptr;
        //TODO: (Req 8) Add an option to deserialize a "MeshRendererComponent" to the following if-else statement
        if(type == CameraComponent::getID()){
            component = entity->addComponent<CameraComponent>();
        } else if (type == FreeCameraControllerComponent::getID()) {
            component = entity->addComponent<FreeCameraControllerComponent>();
        } else if (type == MovementComponent::getID()) {
            component = entity->addComponent<MovementComponent>();
        } else if (type == MeshRendererComponent::getID()) {
            component = entity->addComponent<MeshRendererComponent>();
        } else if (type == MultiMeshRendererComponent::getID()) {
            component = entity->addComponent<MultiMeshRendererComponent>();
        } else if (type == TrackHeightfieldComponent::getID()) {
            component = entity->addComponent<TrackHeightfieldComponent>();
        } else if (type == CarControllerComponent::getID()) {
            component = entity->addComponent<CarControllerComponent>();
        } else if (type == ChaseCameraComponent::getID()) {
            component = entity->addComponent<ChaseCameraComponent>();
        } else if (type == WheelSpinComponent::getID()) {
            component = entity->addComponent<WheelSpinComponent>();
        }
        if(component) component->deserialize(data);
    }

}