#include "forward-renderer.hpp"
#include "../mesh/mesh-utils.hpp"
#include "../texture/texture-utils.hpp"
#include "../components/multi-mesh-renderer.hpp"
#include "../deserialize-utils.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace our {

    struct UploadedLight {
        int type = 1; // 0=Directional, 1=Point, 2=Spot
        glm::vec3 color = glm::vec3(1.0f);
        float intensity = 1.0f;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 attenuation = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec2 coneCos = glm::vec2(0.0f); // (innerCos, outerCos)
        int castsShadows = 0;
    };

    static glm::vec3 worldPositionWithOffset(const glm::mat4& localToWorld, const glm::vec3& localOffset){
        return glm::vec3(localToWorld * glm::vec4(localOffset, 1.0f));
    }

    static glm::vec3 worldForwardDirection(const glm::mat4& localToWorld){
        glm::vec3 dir = glm::vec3(localToWorld * glm::vec4(0, 0, -1, 0));
        float len = glm::length(dir);
        if(len <= 0.00001f) return glm::vec3(0, 0, -1);
        return dir / len;
    }

    void ForwardRenderer::initialize(glm::ivec2 windowSize, const nlohmann::json& config){
        // First, we store the window size for later use
        this->windowSize = windowSize;

        sunSphere = nullptr;
        sunMaterial = nullptr;

        // Optional ambient settings used by lit shaders
        if(config.contains("ambient") && config["ambient"].is_object()){
            const auto& a = config["ambient"];
            ambientColor = a.value("color", ambientColor);
            ambientIntensity = a.value("intensity", ambientIntensity);
        }

        // Then we check if there is a sky texture in the configuration
        if(config.contains("sky")){
            // First, we create a sphere which will be used to draw the sky
            this->skySphere = mesh_utils::sphere(glm::ivec2(16, 16));
            
            // We can draw the sky using the same shader used to draw textured objects
            ShaderProgram* skyShader = new ShaderProgram();
            skyShader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
            skyShader->attach("assets/shaders/textured.frag", GL_FRAGMENT_SHADER);
            skyShader->link();
            
            //TODO: (Req 10) Pick the correct pipeline state to draw the sky
            // Hints: the sky will be draw after the opaque objects so we would need depth testing but which depth funtion should we pick?
            // We will draw the sphere from the inside, so what options should we pick for the face culling.
            PipelineState skyPipelineState{};
            skyPipelineState.depthTesting.enabled = true;
            skyPipelineState.depthTesting.function = GL_LEQUAL;
            skyPipelineState.faceCulling.enabled = true;
            skyPipelineState.faceCulling.culledFace = GL_BACK;
            skyPipelineState.faceCulling.frontFace = GL_CW;
            
            // Load the sky texture (note that we don't need mipmaps since we want to avoid any unnecessary blurring while rendering the sky)
            std::string skyTextureFile = config.value<std::string>("sky", "");
            Texture2D* skyTexture = texture_utils::loadImage(skyTextureFile, false);

            // Setup a sampler for the sky 
            Sampler* skySampler = new Sampler();
            skySampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_WRAP_S, GL_REPEAT);
            skySampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Combine all the aforementioned objects (except the mesh) into a material 
            this->skyMaterial = new TexturedMaterial();
            this->skyMaterial->shader = skyShader;
            this->skyMaterial->texture = skyTexture;
            this->skyMaterial->sampler = skySampler;
            this->skyMaterial->pipelineState = skyPipelineState;
            this->skyMaterial->tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            this->skyMaterial->alphaThreshold = 1.0f;
            this->skyMaterial->transparent = false;
        }

        // Optional sun sphere (drawn behind all geometry but in front of the sky)
        if(config.contains("sun")){
            std::string sunTextureFile = config.value<std::string>("sun", "");
            if(!sunTextureFile.empty()){
                if(config.contains("sunDirection")){
                    // Ray direction (from sun -> scene)
                    glm::vec3 dir = config.value("sunDirection", sunDirectionWorld);
                    float len = glm::length(dir);
                    if(len > 0.0001f) sunDirectionWorld = dir / len;
                } else if(config.contains("sunWorldPosition")){
                    // Backward-compat: if a world position is provided, treat it as "direction to sun".
                    // Convert to ray direction by flipping the sign.
                    glm::vec3 toSun = config.value("sunWorldPosition", glm::vec3(0.0f, 1.0f, 0.0f));
                    float len = glm::length(toSun);
                    if(len > 0.0001f) sunDirectionWorld = (-toSun) / len;
                }
                sunDistance = config.value("sunDistance", sunDistance);
                sunScale = config.value("sunScale", sunScale);

                enableSunLight = config.value("sunLightEnabled", true);
                sunLightColor = config.value("sunLightColor", sunLightColor);
                sunLightIntensity = config.value("sunLightIntensity", sunLightIntensity);

                sunSphere = mesh_utils::sphere(glm::ivec2(32, 32));

                ShaderProgram* sunShader = new ShaderProgram();
                sunShader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
                sunShader->attach("assets/shaders/textured.frag", GL_FRAGMENT_SHADER);
                sunShader->link();

                PipelineState sunPipelineState{};
                sunPipelineState.depthTesting.enabled = true;
                sunPipelineState.depthTesting.function = GL_LEQUAL;
                sunPipelineState.depthMask = false;
                sunPipelineState.blending.enabled = true;
                sunPipelineState.blending.equation = GL_FUNC_ADD;
                sunPipelineState.blending.sourceFactor = GL_SRC_ALPHA;
                sunPipelineState.blending.destinationFactor = GL_ONE_MINUS_SRC_ALPHA;
                sunPipelineState.faceCulling.enabled = true;
                sunPipelineState.faceCulling.culledFace = GL_BACK;
                sunPipelineState.faceCulling.frontFace = GL_CCW;

                Texture2D* sunTexture = texture_utils::loadImage(sunTextureFile, true);

                Sampler* sunSampler = new Sampler();
                sunSampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                sunSampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                sunSampler->set(GL_TEXTURE_WRAP_S, GL_REPEAT);
                sunSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                sunMaterial = new TexturedMaterial();
                sunMaterial->shader = sunShader;
                sunMaterial->texture = sunTexture;
                sunMaterial->sampler = sunSampler;
                sunMaterial->pipelineState = sunPipelineState;
                const float sunTint = config.value("sunTint", 2.5f);
                sunMaterial->tint = glm::vec4(sunTint, sunTint, sunTint, 1.0f);
                sunMaterial->alphaThreshold = 1.0f;
                sunMaterial->transparent = false;
            }
        }

        // Then we check if there is a postprocessing shader in the configuration
        if(config.contains("postprocess")){
            //TODO: (Req 11) Create a framebuffer
            glGenFramebuffers(1, &postprocessFrameBuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, postprocessFrameBuffer);

            //TODO: (Req 11) Create a color and a depth texture and attach them to the framebuffer
            // Hints: The color format can be (Red, Green, Blue and Alpha components with 8 bits for each channel).
            // The depth format can be (Depth component with 24 bits).
            colorTarget = texture_utils::empty(GL_RGBA, windowSize);
            depthTarget = texture_utils::empty(GL_DEPTH_COMPONENT, windowSize);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTarget->getOpenGLName(), 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTarget->getOpenGLName(), 0);
            
            //TODO: (Req 11) Unbind the framebuffer just to be safe
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // Create a vertex array to use for drawing the texture
            glGenVertexArrays(1, &postProcessVertexArray);

            // Create a sampler to use for sampling the scene texture in the post processing shader
            Sampler* postprocessSampler = new Sampler();
            postprocessSampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            postprocessSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Create the post processing shader
            ShaderProgram* postprocessShader = new ShaderProgram();
            postprocessShader->attach("assets/shaders/fullscreen.vert", GL_VERTEX_SHADER);
            postprocessShader->attach(config.value<std::string>("postprocess", ""), GL_FRAGMENT_SHADER);
            postprocessShader->link();

            // Create a post processing material
            postprocessMaterial = new TexturedMaterial();
            postprocessMaterial->shader = postprocessShader;
            postprocessMaterial->texture = colorTarget;
            postprocessMaterial->sampler = postprocessSampler;
            // The default options are fine but we don't need to interact with the depth buffer
            // so it is more performant to disable the depth mask
            postprocessMaterial->pipelineState.depthMask = false;
        }
    }

    void ForwardRenderer::destroy(){
        // Delete all objects related to the sky
        if(skyMaterial){
            delete skySphere;
            delete skyMaterial->shader;
            delete skyMaterial->texture;
            delete skyMaterial->sampler;
            delete skyMaterial;
        }

        // Delete all objects related to the sun
        if(sunMaterial){
            delete sunSphere;
            delete sunMaterial->shader;
            delete sunMaterial->texture;
            delete sunMaterial->sampler;
            delete sunMaterial;
        }
        // Delete all objects related to post processing
        if(postprocessMaterial){
            glDeleteFramebuffers(1, &postprocessFrameBuffer);
            glDeleteVertexArrays(1, &postProcessVertexArray);
            delete colorTarget;
            delete depthTarget;
            delete postprocessMaterial->sampler;
            delete postprocessMaterial->shader;
            delete postprocessMaterial;
        }
    }

    void ForwardRenderer::render(World* world){
        // First of all, we search for a camera and for all the mesh renderers
        CameraComponent* camera = nullptr;
        opaqueCommands.clear();
        transparentCommands.clear();

        // Collect lights for the current frame
        std::vector<UploadedLight> lights;
        lights.reserve(MAX_LIGHTS);

        for(auto entity : world->getEntities()){
            // If we hadn't found a camera yet, we look for a camera in this entity
            if(!camera) camera = entity->getComponent<CameraComponent>();

            if(auto light = entity->getComponent<LightComponent>(); light){
                if((int)lights.size() < MAX_LIGHTS){
                    UploadedLight l;
                    l.type = (int)light->lightType;
                    l.color = light->color;
                    l.intensity = light->intensity;
                    l.attenuation = light->attenuation;
                    l.castsShadows = light->castsShadows ? 1 : 0;

                    const glm::mat4 localToWorld = entity->getLocalToWorldMatrix();
                    l.position = worldPositionWithOffset(localToWorld, light->positionOffset);
                    l.direction = worldForwardDirection(localToWorld);
                    if(light->invertDirection) l.direction = -l.direction;

                    const float innerRad = glm::radians(light->innerAngle);
                    const float outerRad = glm::radians(light->outerAngle);
                    l.coneCos = glm::vec2(std::cos(innerRad), std::cos(outerRad));

                    lights.push_back(l);
                }
            }

            // If this entity has a multi-mesh renderer component
            if(auto multi = entity->getComponent<MultiMeshRendererComponent>(); multi){
                for(const auto& part : multi->parts){
                    if(part.mesh == nullptr || part.material == nullptr) continue;
                    RenderCommand command;
                    command.localToWorld = multi->getOwner()->getLocalToWorldMatrix() * part.localTransform.toMat4();
                    command.center = glm::vec3(command.localToWorld * glm::vec4(0, 0, 0, 1));
                    command.mesh = part.mesh;
                    command.material = part.material;
                    if(command.material->transparent){
                        transparentCommands.push_back(command);
                    } else {
                        opaqueCommands.push_back(command);
                    }
                }
            }

            // If this entity has a mesh renderer component
            if(auto meshRenderer = entity->getComponent<MeshRendererComponent>(); meshRenderer){
                // If either the mesh or material is missing, skip this renderer.
                // (Mesh/material pointers come from AssetLoader and can be nullptr if the asset name is wrong or failed to load.)
                if(meshRenderer->mesh == nullptr || meshRenderer->material == nullptr) continue;
                // We construct a command from it
                RenderCommand command;
                command.localToWorld = meshRenderer->getOwner()->getLocalToWorldMatrix();
                command.center = glm::vec3(command.localToWorld * glm::vec4(0, 0, 0, 1));
                command.mesh = meshRenderer->mesh;
                command.material = meshRenderer->material;
                // if it is transparent, we add it to the transparent commands list
                if(command.material->transparent){
                    transparentCommands.push_back(command);
                } else {
                // Otherwise, we add it to the opaque command list
                    opaqueCommands.push_back(command);
                }
            }
        }

        // Optional sun light from renderer config (keeps lighting aligned with the sun sprite)
        if(enableSunLight && sunMaterial && (int)lights.size() < MAX_LIGHTS){
            UploadedLight sun;
            sun.type = 0; // directional
            sun.color = sunLightColor;
            sun.intensity = sunLightIntensity;
            // lightDirection is the rays direction (from light to scene)
            sun.direction = sunDirectionWorld;
            lights.push_back(sun);
        }

        // If there is no camera, we return (we cannot render without a camera)
        if(camera == nullptr) return;

        //TODO: (Req 9) Modify the following line such that "cameraForward" contains a vector pointing the camera forward direction
        // HINT: See how you wrote the CameraComponent::getViewMatrix, it should help you solve this one
        auto M = camera->getOwner()->getLocalToWorldMatrix();
        glm::vec3 eye = glm::vec3(M * glm::vec4(0, 0, 0, 1));
        glm::vec3 center = glm::vec3(M * glm::vec4(0, 0, -1, 1));
        glm::vec3 cameraForward = glm::normalize(center - eye);
        std::sort(transparentCommands.begin(), transparentCommands.end(), [cameraForward](const RenderCommand& first, const RenderCommand& second){
            //TODO: (Req 9) Finish this function
            // HINT: the following return should return true "first" should be drawn before "second". 
            float firstDistance = glm::dot(first.center, cameraForward);
            float secondDistance = glm::dot(second.center, cameraForward);
            return firstDistance > secondDistance;
        });

        //TODO: (Req 9) Get the camera ViewProjection matrix and store it in VP
        glm::mat4 VP = camera->getProjectionMatrix(windowSize) * camera->getViewMatrix();

        //TODO: (Req 9) Set the OpenGL viewport using viewportStart and viewportSize
        glViewport(0, 0, windowSize.x, windowSize.y);
        
        //TODO: (Req 9) Set the clear color to black and the clear depth to 1
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0f);
        
        //TODO: (Req 9) Set the color mask to true and the depth mask to true (to ensure the glClear will affect the framebuffer)
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);

        // If there is a postprocess material, bind the framebuffer
        if(postprocessMaterial){
            //TODO: (Req 11) bind the framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, postprocessFrameBuffer);
        }

        //TODO: (Req 9) Clear the color and depth buffers
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        //TODO: (Req 9) Draw all the opaque commands
        // Don't forget to set the "transform" uniform to be equal the model-view-projection matrix for each render command
        for(auto& command : opaqueCommands){
            glm::mat4 transform = VP * command.localToWorld;
            command.material->setup();
            command.material->shader->set("transform", transform);
            command.material->shader->set("model", command.localToWorld);
            command.material->shader->set("cameraPosition", eye);
            command.material->shader->set("ambientColor", ambientColor);
            command.material->shader->set("ambientIntensity", ambientIntensity);

            command.material->shader->set("lightCount", (GLint)lights.size());
            for(size_t i = 0; i < lights.size(); i++){
                const auto& l = lights[i];
                const std::string idx = std::to_string(i);
                command.material->shader->set("lightType[" + idx + "]", (GLint)l.type);
                command.material->shader->set("lightColor[" + idx + "]", l.color);
                command.material->shader->set("lightIntensity[" + idx + "]", l.intensity);
                command.material->shader->set("lightPosition[" + idx + "]", l.position);
                command.material->shader->set("lightDirection[" + idx + "]", l.direction);
                command.material->shader->set("lightAttenuation[" + idx + "]", l.attenuation);
                command.material->shader->set("lightConeCos[" + idx + "]", l.coneCos);
                command.material->shader->set("lightCastsShadows[" + idx + "]", (GLint)l.castsShadows);
            }
            command.mesh->draw();
        }
        // If there is a sky material, draw the sky
        if(this->skyMaterial){
            //TODO: (Req 10) setup the sky material
            skyMaterial->setup();
            //TODO: (Req 10) Get the camera position
            glm::vec3 camPosition = eye;

            //TODO: (Req 10) Create a model matrix for the sy such that it always follows the camera (sky sphere center = camera position)
            glm::mat4 skyModelMatrix = glm::translate(glm::mat4(1.0f), camPosition);

            //TODO: (Req 10) We want the sky to be drawn behind everything (in NDC space, z=1)
            // We can acheive the is by multiplying by an extra matrix after the projection but what values should we put in it?
            glm::mat4 alwaysBehindTransform = glm::mat4(
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 1.0f
            );
            //TODO: (Req 10) set the "transform" uniform
            skyMaterial->shader->set("transform", alwaysBehindTransform * camera->getProjectionMatrix(windowSize) * camera->getViewMatrix() * skyModelMatrix);
            //TODO: (Req 10) draw the sky sphere
            skySphere->draw();
        }

        // If there is a sun material, draw the sun sphere in front of the sky but behind all geometry
        if(this->sunMaterial){
            sunMaterial->setup();

            // Directional sun: fixed world ray direction. Visual sun is opposite the ray direction.
            glm::vec3 sunCenter = eye + (-sunDirectionWorld) * sunDistance;
            glm::mat4 sunModelMatrix = glm::translate(glm::mat4(1.0f), sunCenter) * glm::scale(glm::mat4(1.0f), glm::vec3(sunScale));

            glm::mat4 alwaysBehindTransform = glm::mat4(
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 1.0f
            );

            sunMaterial->shader->set("transform", alwaysBehindTransform * camera->getProjectionMatrix(windowSize) * camera->getViewMatrix() * sunModelMatrix);
            sunSphere->draw();
        }
        //TODO: (Req 9) Draw all the transparent commands
        // Don't forget to set the "transform" uniform to be equal the model-view-projection matrix for each render command
        for(auto& command : transparentCommands){
            glm::mat4 transform = VP * command.localToWorld;
            command.material->setup();
            command.material->shader->set("transform", transform);
            command.material->shader->set("model", command.localToWorld);
            command.material->shader->set("cameraPosition", eye);
            command.material->shader->set("ambientColor", ambientColor);
            command.material->shader->set("ambientIntensity", ambientIntensity);

            command.material->shader->set("lightCount", (GLint)lights.size());
            for(size_t i = 0; i < lights.size(); i++){
                const auto& l = lights[i];
                const std::string idx = std::to_string(i);
                command.material->shader->set("lightType[" + idx + "]", (GLint)l.type);
                command.material->shader->set("lightColor[" + idx + "]", l.color);
                command.material->shader->set("lightIntensity[" + idx + "]", l.intensity);
                command.material->shader->set("lightPosition[" + idx + "]", l.position);
                command.material->shader->set("lightDirection[" + idx + "]", l.direction);
                command.material->shader->set("lightAttenuation[" + idx + "]", l.attenuation);
                command.material->shader->set("lightConeCos[" + idx + "]", l.coneCos);
                command.material->shader->set("lightCastsShadows[" + idx + "]", (GLint)l.castsShadows);
            }
            command.mesh->draw();
        }

        // If there is a postprocess material, apply postprocessing
        if(postprocessMaterial){
            //TODO: (Req 11) Return to the default framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            //TODO: (Req 11) Setup the postprocess material and draw the fullscreen triangle
            postprocessMaterial->setup();
            postprocessMaterial->shader->set("transform", glm::mat4(1.0f));
            glBindVertexArray(postProcessVertexArray);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

}