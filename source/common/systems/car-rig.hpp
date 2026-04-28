#pragma once

#include "../ecs/world.hpp"
#include "../components/car-rig.hpp"
#include "../components/multi-mesh-renderer.hpp"
#include "../components/car-controller.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace our {

    class CarRigSystem {

        static glm::vec3 getForwardFromYaw(float yaw){
            glm::mat4 rot = glm::yawPitchRoll(yaw, 0.0f, 0.0f);
            return glm::vec3(rot * glm::vec4(0, 0, 1, 0));
        }

        static int estimateWheelAxisLocal(MultiMeshRendererComponent::Node* node, const MultiMeshRendererComponent& multi){
            if(!node || node->partIndices.empty()) return 0;
            const glm::vec3 s = glm::abs(multi.parts[node->partIndices[0]].aabbSize);
            if(s.x <= s.y && s.x <= s.z) return 0;
            if(s.y <= s.x && s.y <= s.z) return 1;
            return 2;
        }

        static float estimateWheelRadiusLocal(MultiMeshRendererComponent::Node* node, const MultiMeshRendererComponent& multi, int axis){
            if(!node || node->partIndices.empty()) return 0.3f;
            const glm::vec3 s = glm::abs(multi.parts[node->partIndices[0]].aabbSize);
            if(axis == 0) return 0.5f * std::max(s.y, s.z);
            if(axis == 1) return 0.5f * std::max(s.x, s.z);
            return 0.5f * std::max(s.x, s.y);
        }

        static float estimateUniformScale(const Transform& t){
            return std::max(std::abs(t.scale.x), std::max(std::abs(t.scale.y), std::abs(t.scale.z)));
        }

        static void printAvailableNodes(MultiMeshRendererComponent::Node* root, int depth = 0) {
            if(!root) return;
            std::string indent(depth * 2, ' ');
            std::cerr << indent << "- " << root->name << std::endl;
            for(auto* c : root->children) printAvailableNodes(c, depth + 1);
        }

        static void loadCarRig(CarRigComponent& rig, const MultiMeshRendererComponent& multi){
            rig.resolvedWheels.clear();
            rig.resolvedWheels.reserve(rig.wheels.size());

            std::cerr << "[CarRig] Binding wheels for model: " << multi.sourceObjPath << std::endl;

            for(const auto& wheelCfg : rig.wheels){
                CarRigComponent::ResolvedWheel resolved;
                std::cerr << "  Wheel: " << (wheelCfg.label.empty() ? "unnamed" : wheelCfg.label) << std::endl;

                if(!wheelCfg.steerNode.empty()){
                    auto* node = multi.findNodeRecursive(multi.rootNode, wheelCfg.steerNode);
                    if(node){
                        resolved.steerNodePtr = node;
                        resolved.steerOriginalTransform = node->localTransform;
                        std::cerr << "    - Steer node resolved: " << node->name << std::endl;
                    } else {
                        std::cerr << "[CarRig] WARNING: steer node \"" << wheelCfg.steerNode
                                  << "\" not found in model \"" << multi.sourceObjPath << "\"!" << std::endl;
                        std::cerr << "Available nodes:" << std::endl;
                        printAvailableNodes(multi.rootNode);
                    }
                }

                for(const auto& spinName : wheelCfg.spinNodes){
                    auto* node = multi.findNodeRecursive(multi.rootNode, spinName);
                    if(node){
                        CarRigComponent::SpinEntry entry;
                        entry.nodePtr = node;
                        entry.originalTransform = node->localTransform;
                        resolved.spinEntries.push_back(entry);
                        std::cerr << "    - Spin node resolved: " << node->name << std::endl;
                    } else {
                        std::cerr << "[CarRig] WARNING: spin node \"" << spinName
                                  << "\" not found in model \"" << multi.sourceObjPath << "\"!" << std::endl;
                        std::cerr << "Available nodes:" << std::endl;
                        printAvailableNodes(multi.rootNode);
                    }
                }

                for(const auto& staticName : wheelCfg.staticNodes){
                    auto* node = multi.findNodeRecursive(multi.rootNode, staticName);
                    if(!node){
                        std::cerr << "[CarRig] WARNING: static node \"" << staticName
                                  << "\" not found in model \"" << multi.sourceObjPath << "\"" << std::endl;
                    }
                }

                rig.resolvedWheels.push_back(std::move(resolved));
            }

            rig._bound = true;
        }

        static void applySteering(MultiMeshRendererComponent::Node* node, const Transform& original, float steerAngle){
            if(!node) return;
            node->localTransform.rotation = original.rotation;
            node->localTransform.rotation.y = original.rotation.y + steerAngle;
        }

        static void applySpin(MultiMeshRendererComponent::Node* node, const Transform& original, int axis, float spinAngle){
            if(!node) return;
            node->localTransform.rotation = original.rotation;
            if(axis == 0) node->localTransform.rotation.x = original.rotation.x + spinAngle;
            else if(axis == 1) node->localTransform.rotation.y = original.rotation.y + spinAngle;
            else node->localTransform.rotation.z = original.rotation.z + spinAngle;
        }

        static void applySteerAndSpin(MultiMeshRendererComponent::Node* node, const Transform& original, float steerAngle, int spinAxis, float spinAngle){
            if(!node) return;
            node->localTransform.rotation = original.rotation;
            node->localTransform.rotation.y = original.rotation.y + steerAngle;
            if(spinAxis == 0) node->localTransform.rotation.x = original.rotation.x + spinAngle;
            else if(spinAxis == 1) node->localTransform.rotation.y += spinAngle;
            else node->localTransform.rotation.z = original.rotation.z + spinAngle;
        }

    public:
        void update(World* world, float deltaTime){
            if(world == nullptr) return;

            for(auto entity : world->getEntities()){
                auto* rig = entity->getComponent<CarRigComponent>();
                if(rig == nullptr) continue;

                auto* multi = entity->getComponent<MultiMeshRendererComponent>();
                if(multi == nullptr || !multi->preserveHierarchy || multi->rootNode == nullptr) continue;

                if(!rig->_bound){
                    loadCarRig(*rig, *multi);
                }

                if(rig->resolvedWheels.empty()) continue;

                auto* car = entity->getComponent<CarControllerComponent>();
                float steerAngle = 0.0f;
                if(car != nullptr){
                    steerAngle = car->steeringAngle;
                }

                const glm::vec3 currentPos = entity->localTransform.position;
                if(!rig->_hasLastPosition){
                    rig->_hasLastPosition = true;
                    rig->_lastWorldPosition = currentPos;
                    continue;
                }

                const glm::vec3 deltaPos = currentPos - rig->_lastWorldPosition;
                rig->_lastWorldPosition = currentPos;

                const glm::vec2 deltaXZ(deltaPos.x, deltaPos.z);
                const float dist = glm::length(deltaXZ);

                float spinDelta = 0.0f;
                if(dist > 1e-6f){
                    float sign = 1.0f;
                    if(car != nullptr){
                        if(car->speed < -1e-4f) sign = -1.0f;
                    } else {
                        const glm::vec3 forward = getForwardFromYaw(entity->localTransform.rotation.y);
                        const glm::vec2 forwardXZ(forward.x, forward.z);
                        if(glm::length(forwardXZ) > 1e-6f){
                            const float d = glm::dot(glm::normalize(deltaXZ), glm::normalize(forwardXZ));
                            if(d < 0.0f) sign = -1.0f;
                        }
                    }

                    float radiusWorld = 0.3f;
                    const float scale = estimateUniformScale(entity->localTransform);
                    for(const auto& rw : rig->resolvedWheels){
                        for(const auto& se : rw.spinEntries){
                            auto* node = static_cast<MultiMeshRendererComponent::Node*>(se.nodePtr);
                            if(node){
                                const int axis = (rig->spinAxis == -1) ? estimateWheelAxisLocal(node, *multi) : rig->spinAxis;
                                const float rLocal = estimateWheelRadiusLocal(node, *multi, axis);
                                radiusWorld = std::max(1e-4f, rLocal * scale);
                                goto found_radius;
                            }
                        }
                    }
                    found_radius:
                    spinDelta = (dist * sign / radiusWorld) * rig->spinDirection;
                }

                rig->_accumulatedSpinAngle += spinDelta;

                for(auto& rw : rig->resolvedWheels){
                    auto* steerNode = static_cast<MultiMeshRendererComponent::Node*>(rw.steerNodePtr);
                    if(steerNode){
                        bool isAlsoSpin = false;
                        for(const auto& se : rw.spinEntries){
                            if(se.nodePtr == steerNode){
                                isAlsoSpin = true;
                                const int axis = (rig->spinAxis == -1)
                                    ? estimateWheelAxisLocal(steerNode, *multi)
                                    : rig->spinAxis;
                                applySteerAndSpin(steerNode, rw.steerOriginalTransform, steerAngle, axis, rig->_accumulatedSpinAngle);
                                break;
                            }
                        }
                        if(!isAlsoSpin){
                            applySteering(steerNode, rw.steerOriginalTransform, steerAngle);
                        }
                    }

                    for(const auto& se : rw.spinEntries){
                        auto* spinNode = static_cast<MultiMeshRendererComponent::Node*>(se.nodePtr);
                        if(!spinNode || spinNode == steerNode) continue;

                        const int axis = (rig->spinAxis == -1)
                            ? estimateWheelAxisLocal(spinNode, *multi)
                            : rig->spinAxis;
                        applySpin(spinNode, se.originalTransform, axis, rig->_accumulatedSpinAngle);
                    }
                }
            }
        }
    };

}
