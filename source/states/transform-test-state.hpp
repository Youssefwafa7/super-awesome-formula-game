#pragma once

#include <shader/shader.hpp>
#include <mesh/mesh.hpp>
#include <mesh/mesh-utils.hpp>
#include <ecs/transform.hpp>
#include <application.hpp>
#include <deserialize-utils.hpp>

#include <vector>
#include <glm/gtc/matrix_transform.hpp> 


// This state test and shows how to use the Transform struct.
class TransformTestState: public our::State {

    our::ShaderProgram* shader;
    our::Mesh* mesh;
    std::vector<our::Transform> transforms;
    glm::mat4 VP;
    
    void onInitialize() override {
        // First of all, we get the scene configuration from the app config
        auto& config = getApp()->getConfig()["scene"];
        // Then we load the shader that will be used for this scene
        shader = new our::ShaderProgram();
        shader->attach("assets/shaders/transform-test.vert", GL_VERTEX_SHADER);
        shader->attach("assets/shaders/transform-test.frag", GL_FRAGMENT_SHADER);
        shader->link();
        // Then we load the mesh
        mesh = our::mesh_utils::loadModel("assets/models/monkey.obj");
        // Then we read a list of transform objects from the shader
        // In draw, we will render a mesh for each of the transforms
        transforms.clear();
        if(config.contains("objects")){
            if(auto& objects = config["objects"]; objects.is_array()){
                for(auto& object : objects){
                    our::Transform transform;
                    transform.deserialize(object);
                    transforms.push_back(transform);
                }
            }
        }
        // Then we read the camera information to compute the VP matrix
        if(config.contains("camera")){
            if(auto& camera = config["camera"]; camera.is_object()){
                auto readVec3 = [](const nlohmann::json& value, const glm::vec3& fallback){
                    if(!value.is_array() || value.size() < 3) return fallback;
                    return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
                };

                glm::vec3 eye = readVec3(camera.value("eye", nlohmann::json::array()), glm::vec3(0, 0, 0));
                glm::vec3 center = readVec3(camera.value("center", nlohmann::json::array()), glm::vec3(0, 0, -1));
                glm::vec3 up = readVec3(camera.value("up", nlohmann::json::array()), glm::vec3(0, 1, 0));
                glm::mat4 V = glm::lookAt(eye, center, up);

                float fov = glm::radians(camera.value("fov", 90.0f));
                float nearPlane = camera.value("near", 0.01f);
                float farPlane = camera.value("far", 1000.0f);

                glm::ivec2 size = getApp()->getFrameBufferSize();
                float aspect = float(size.x)/size.y;
                glm::mat4 P = glm::perspective(fov, aspect, nearPlane, farPlane);

                VP = P * V;
            }
        }
    }

    void onDraw(double deltaTime) override {
        glClear(GL_COLOR_BUFFER_BIT);
        shader->use();
        for(auto& transform : transforms){
            // For each transform, we compute the MVP matrix and send it to the "transform" uniform
            shader->set("transform", VP * transform.toMat4());
            // Then we draw a mesh instance
            mesh->draw();
        }
    }

    void onDestroy() override {
        delete shader;
        delete mesh;
    }
};