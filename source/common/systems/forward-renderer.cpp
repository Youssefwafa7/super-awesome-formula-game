
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

    void ForwardRenderer::initialize(glm::ivec2 windowSize, const nlohmann::json& config, bool isMultiplayer){
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
            this->skySphere = mesh_utils::sphere(glm::ivec2(16, 16));
            ShaderProgram* skyShader = new ShaderProgram();
            skyShader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
            skyShader->attach("assets/shaders/textured.frag", GL_FRAGMENT_SHADER);
            skyShader->link();
            
            PipelineState skyPipelineState{};
            skyPipelineState.depthTesting.enabled = true;
            skyPipelineState.depthTesting.function = GL_LEQUAL;
            skyPipelineState.faceCulling.enabled = true;
            skyPipelineState.faceCulling.culledFace = GL_BACK;
            skyPipelineState.faceCulling.frontFace = GL_CW;
            
            std::string skyTextureFile = config.value<std::string>("sky", "");
            Texture2D* skyTexture = nullptr;
            if(skyTextureFile.size() > 4 && skyTextureFile.substr(skyTextureFile.size() - 4) == ".hdr"){
                skyTexture = texture_utils::loadImageHDR(skyTextureFile);
            } else {
                skyTexture = texture_utils::loadImage(skyTextureFile, false);
            }

            Sampler* skySampler = new Sampler();
            skySampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_WRAP_S, GL_REPEAT);
            skySampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            this->skyMaterial = new TexturedMaterial();
            this->skyMaterial->shader = skyShader;
            this->skyMaterial->texture = skyTexture;
            this->skyMaterial->sampler = skySampler;
            this->skyMaterial->pipelineState = skyPipelineState;
            this->skyMaterial->tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            this->skyMaterial->alphaThreshold = 1.0f;
            this->skyMaterial->transparent = false;
        }

        if(config.contains("sun")){
            std::string sunTextureFile = config.value<std::string>("sun", "");
            if(!sunTextureFile.empty()){
                if(config.contains("sunDirection")){
                    glm::vec3 dir = config.value("sunDirection", sunDirectionWorld);
                    float len = glm::length(dir);
                    if(len > 0.0001f) sunDirectionWorld = dir / len;
                } else if(config.contains("sunWorldPosition")){
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

        if(config.contains("postprocess")){
            glGenFramebuffers(1, &postprocessFrameBuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, postprocessFrameBuffer);
            colorTarget = texture_utils::empty(GL_RGBA, windowSize);
            depthTarget = texture_utils::empty(GL_DEPTH_COMPONENT, windowSize);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTarget->getOpenGLName(), 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTarget->getOpenGLName(), 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // --- MSAA FBO (renderbuffer-backed, dynamic MSAA for performance) ---
            const int msaaSamples = isMultiplayer ? 2 : 4;
            glGenFramebuffers(1, &msaaFrameBuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, msaaFrameBuffer);

            glGenRenderbuffers(1, &msaaColorRenderBuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRenderBuffer);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaaSamples, GL_RGBA8, windowSize.x, windowSize.y);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRenderBuffer);

            glGenRenderbuffers(1, &msaaDepthRenderBuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRenderBuffer);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaaSamples, GL_DEPTH_COMPONENT24, windowSize.x, windowSize.y);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRenderBuffer);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glGenVertexArrays(1, &postProcessVertexArray);
            Sampler* postprocessSampler = new Sampler();
            postprocessSampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            postprocessSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            ShaderProgram* postprocessShader = new ShaderProgram();
            postprocessShader->attach("assets/shaders/fullscreen.vert", GL_VERTEX_SHADER);
            postprocessShader->attach(config.value<std::string>("postprocess", ""), GL_FRAGMENT_SHADER);
            postprocessShader->link();
            postprocessMaterial = new TexturedMaterial();
            postprocessMaterial->shader = postprocessShader;
            postprocessMaterial->texture = colorTarget;
            postprocessMaterial->sampler = postprocessSampler;
            postprocessMaterial->pipelineState.depthMask = false;
        }
    }

    void ForwardRenderer::destroy(){
        if(skyMaterial){
            delete skySphere;
            delete skyMaterial->shader;
            delete skyMaterial->texture;
            delete skyMaterial->sampler;
            delete skyMaterial;
        }
        if(sunMaterial){
            delete sunSphere;
            delete sunMaterial->shader;
            delete sunMaterial->texture;
            delete sunMaterial->sampler;
            delete sunMaterial;
        }
        if(postprocessMaterial){
            glDeleteFramebuffers(1, &postprocessFrameBuffer);
            if(msaaFrameBuffer) {
                glDeleteFramebuffers(1, &msaaFrameBuffer);
                glDeleteRenderbuffers(1, &msaaColorRenderBuffer);
                glDeleteRenderbuffers(1, &msaaDepthRenderBuffer);
            }
            glDeleteVertexArrays(1, &postProcessVertexArray);
            delete colorTarget;
            delete depthTarget;
            delete postprocessMaterial->sampler;
            delete postprocessMaterial->shader;
            delete postprocessMaterial;
        }
    }

    void ForwardRenderer::render(World* world){
        // Collect cameras, commands and lights once per frame
        std::vector<CameraComponent*> cameras;
        opaqueCommands.clear();
        transparentCommands.clear();
        std::vector<UploadedLight> lights;
        lights.reserve(MAX_LIGHTS);

        for(auto entity : world->getEntities()){
            if(auto cam = entity->getComponent<CameraComponent>()) {
                cameras.push_back(cam);
            }

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

            if(auto multi = entity->getComponent<MultiMeshRendererComponent>(); multi){
                for(const auto& part : multi->parts){
                    if(part.mesh == nullptr || part.material == nullptr) continue;
                    RenderCommand command;
                    command.localToWorld = multi->getOwner()->getLocalToWorldMatrix() * part.localTransform.toMat4();
                    command.center = glm::vec3(command.localToWorld * glm::vec4(0, 0, 0, 1));
                    command.mesh = part.mesh;
                    command.material = part.material;
                    if(command.material->transparent) transparentCommands.push_back(command);
                    else opaqueCommands.push_back(command);
                }
            }

            if(auto meshRenderer = entity->getComponent<MeshRendererComponent>(); meshRenderer){
                if(meshRenderer->mesh == nullptr || meshRenderer->material == nullptr) continue;
                RenderCommand command;
                command.localToWorld = meshRenderer->getOwner()->getLocalToWorldMatrix();
                command.center = glm::vec3(command.localToWorld * glm::vec4(0, 0, 0, 1));
                command.mesh = meshRenderer->mesh;
                command.material = meshRenderer->material;
                if(command.material->transparent) transparentCommands.push_back(command);
                else opaqueCommands.push_back(command);
            }
        }

        if(enableSunLight && sunMaterial && (int)lights.size() < MAX_LIGHTS){
            UploadedLight sun;
            sun.type = 0; // directional
            sun.color = sunLightColor;
            sun.intensity = sunLightIntensity;
            sun.direction = sunDirectionWorld;
            lights.push_back(sun);
        }

        if(cameras.empty()) return;

        // --- CLEARING ---
        // We set the color mask and depth mask to true to ensure clear affects the framebuffer
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0f);

        if(postprocessMaterial){
            if(msaaFrameBuffer)
                glBindFramebuffer(GL_FRAMEBUFFER, msaaFrameBuffer);
            else
                glBindFramebuffer(GL_FRAMEBUFFER, postprocessFrameBuffer);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- RENDERING ---
        for(size_t camIdx = 0; camIdx < cameras.size(); camIdx++){
            CameraComponent* camera = cameras[camIdx];
            
            // Viewport management
            glm::ivec2 vpSize = windowSize;
            if(cameras.size() == 2){
                vpSize.y /= 2;
                if(camIdx == 0) glViewport(0, windowSize.y / 2, windowSize.x, windowSize.y / 2);
                else glViewport(0, 0, windowSize.x, windowSize.y / 2);
            } else {
                glViewport(0, 0, windowSize.x, windowSize.y);
            }

            auto M = camera->getOwner()->getLocalToWorldMatrix();
            glm::vec3 eye = glm::vec3(M * glm::vec4(0, 0, 0, 1));
            glm::vec3 center = glm::vec3(M * glm::vec4(0, 0, -1, 1));
            glm::vec3 cameraForward = glm::normalize(center - eye);

            // Sort transparent commands back-to-front
            std::sort(transparentCommands.begin(), transparentCommands.end(), [cameraForward](const RenderCommand& first, const RenderCommand& second){
                return glm::dot(first.center, cameraForward) > glm::dot(second.center, cameraForward);
            });

            glm::mat4 VP = camera->getProjectionMatrix(vpSize) * camera->getViewMatrix();

            auto setupCommonUniforms = [&](Material* mat, const glm::mat4& localToWorld){
                mat->setup();
                mat->shader->set("transform", VP * localToWorld);
                mat->shader->set("model", localToWorld);
                mat->shader->set("cameraPosition", eye);
                mat->shader->set("ambientColor", ambientColor);
                mat->shader->set("ambientIntensity", ambientIntensity);
                mat->shader->set("lightCount", (GLint)lights.size());
                for(size_t i = 0; i < lights.size(); i++){
                    const auto& l = lights[i];
                    const std::string idx = std::to_string(i);
                    mat->shader->set("lightType[" + idx + "]", (GLint)l.type);
                    mat->shader->set("lightColor[" + idx + "]", l.color);
                    mat->shader->set("lightIntensity[" + idx + "]", l.intensity);
                    mat->shader->set("lightPosition[" + idx + "]", l.position);
                    mat->shader->set("lightDirection[" + idx + "]", l.direction);
                    mat->shader->set("lightAttenuation[" + idx + "]", l.attenuation);
                    mat->shader->set("lightConeCos[" + idx + "]", l.coneCos);
                    mat->shader->set("lightCastsShadows[" + idx + "]", (GLint)l.castsShadows);
                }
            };

            for(auto& command : opaqueCommands){
                setupCommonUniforms(command.material, command.localToWorld);
                command.mesh->draw();
            }

            if(this->skyMaterial){
                skyMaterial->setup();
                glm::mat4 skyModelMatrix = glm::translate(glm::mat4(1.0f), eye);
                glm::mat4 alwaysBehindTransform = glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
                skyMaterial->shader->set("transform", alwaysBehindTransform * camera->getProjectionMatrix(vpSize) * camera->getViewMatrix() * skyModelMatrix);
                skySphere->draw();
            }

            if(this->sunMaterial){
                sunMaterial->setup();
                glm::vec3 sunCenter = eye + (-sunDirectionWorld) * sunDistance;
                glm::mat4 sunModelMatrix = glm::translate(glm::mat4(1.0f), sunCenter) * glm::scale(glm::mat4(1.0f), glm::vec3(sunScale));
                glm::mat4 alwaysBehindTransform = glm::mat4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
                sunMaterial->shader->set("transform", alwaysBehindTransform * camera->getProjectionMatrix(vpSize) * camera->getViewMatrix() * sunModelMatrix);
                sunSphere->draw();
            }

            for(auto& command : transparentCommands){
                setupCommonUniforms(command.material, command.localToWorld);
                command.mesh->draw();
            }
        }

        // Apply postprocessing once at the end
        if(postprocessMaterial){
            // Resolve MSAA: blit the multisampled FBO into the resolve (texture-backed) FBO
            if(msaaFrameBuffer) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFrameBuffer);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postprocessFrameBuffer);
                glBlitFramebuffer(0, 0, windowSize.x, windowSize.y,
                                  0, 0, windowSize.x, windowSize.y,
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            postprocessMaterial->setup();
            postprocessMaterial->shader->set("transform", glm::mat4(1.0f));
            postprocessMaterial->shader->set("enable_vignette", (int)enableVignette);
            postprocessMaterial->shader->set("enable_chromatic_aberration", (int)enableChromaticAberration);
            postprocessMaterial->shader->set("speed_factor", speedFactor);
            glBindVertexArray(postProcessVertexArray);
            glViewport(0, 0, windowSize.x, windowSize.y);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
}