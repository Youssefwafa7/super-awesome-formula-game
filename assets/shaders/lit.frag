#version 330 core

in VS_OUT {
    vec4 color;
    vec2 tex_coord;
    vec3 world_pos;
    vec3 world_normal;
} fs_in;

out vec4 frag_color;

// Material
uniform vec4 tint;

uniform sampler2D albedoMap;
uniform sampler2D specularMap;
uniform sampler2D roughnessMap;
uniform sampler2D aoMap;
uniform sampler2D emissionMap;

uniform int hasAlbedoMap;
uniform int hasSpecularMap;
uniform int hasRoughnessMap;
uniform int hasAoMap;
uniform int hasEmissionMap;

uniform vec3 albedoColor;
uniform vec3 specularColor;
uniform float roughnessValue;
uniform float metallicValue;
uniform float aoValue;
uniform vec3 emissionColor;
uniform float emissionIntensity;
uniform float alphaThreshold;
uniform int useBlinnPhong;

// Camera
uniform vec3 cameraPosition;

// Ambient
uniform vec3 ambientColor;
uniform float ambientIntensity;

// Lights
#define MAX_LIGHTS 16

uniform int lightCount;
uniform int lightType[MAX_LIGHTS];
uniform vec3 lightColor[MAX_LIGHTS];
uniform float lightIntensity[MAX_LIGHTS];
uniform vec3 lightPosition[MAX_LIGHTS];
uniform vec3 lightDirection[MAX_LIGHTS];
uniform vec3 lightAttenuation[MAX_LIGHTS]; // (constant, linear, quadratic)
uniform vec2 lightConeCos[MAX_LIGHTS];     // (innerCos, outerCos)
uniform int lightCastsShadows[MAX_LIGHTS];

vec3 srgbToLinear(vec3 c){
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 linearToSrgb(vec3 c){
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 getAlbedo(){
    vec3 sampled = vec3(1.0);
    if(hasAlbedoMap != 0) sampled = srgbToLinear(texture(albedoMap, fs_in.tex_coord).rgb);
    return sampled * albedoColor * fs_in.color.rgb * tint.rgb;
}

vec3 getSpecular(){
    vec3 sampled = vec3(1.0);
    if(hasSpecularMap != 0) sampled = srgbToLinear(texture(specularMap, fs_in.tex_coord).rgb);
    return sampled * specularColor;
}

float getRoughness(){
    float sampled = 1.0;
    if(hasRoughnessMap != 0) sampled = texture(roughnessMap, fs_in.tex_coord).r;
    return clamp(sampled * roughnessValue, 0.02, 1.0);
}

float getAO(){
    float sampled = 1.0;
    if(hasAoMap != 0) sampled = texture(aoMap, fs_in.tex_coord).r;
    return clamp(sampled * aoValue, 0.0, 1.0);
}

vec3 getEmission(){
    vec3 sampled = vec3(1.0);
    if(hasEmissionMap != 0) sampled = srgbToLinear(texture(emissionMap, fs_in.tex_coord).rgb);
    return sampled * emissionColor * emissionIntensity;
}

float attenuatePoint(vec3 att, float d){
    return 1.0 / max(att.x + att.y * d + att.z * d * d, 0.0001);
}

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0000001); // prevent divide by zero
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main(){
    float albedoAlpha = 1.0;
    if(hasAlbedoMap != 0){
        albedoAlpha = texture(albedoMap, fs_in.tex_coord).a;
    }
    if(albedoAlpha < alphaThreshold) discard;

    vec3 N = normalize(fs_in.world_normal);
    vec3 V = normalize(cameraPosition - fs_in.world_pos);

    vec3 albedo = getAlbedo();
    vec3 specCol = getSpecular();
    float rough = getRoughness();
    float metallic = clamp(metallicValue, 0.0, 1.0);

    vec3 F0 = mix(specCol, albedo, metallic);

    float ao = getAO();

    vec3 ambient = ambientColor * ambientIntensity * albedo * ao;

    vec3 Lo = vec3(0.0);

    int count = clamp(lightCount, 0, MAX_LIGHTS);
    for(int i = 0; i < count; i++){
        vec3 L;
        float attenuation = 1.0;

        if(lightType[i] == 0){
            // Directional: lightDirection is the direction the light points (rays direction)
            L = normalize(-lightDirection[i]);
        } else {
            vec3 toLight = lightPosition[i] - fs_in.world_pos;
            float d = length(toLight);
            if(d <= 0.0001) continue;
            L = toLight / d;
            attenuation = attenuatePoint(lightAttenuation[i], d);
        }

        // Spotlight factor
        float spotFactor = 1.0;
        if(lightType[i] == 2){
            vec3 lightDir = normalize(lightDirection[i]);
            vec3 lightToFrag = normalize(fs_in.world_pos - lightPosition[i]);
            float cosTheta = dot(lightToFrag, lightDir);
            float innerCos = lightConeCos[i].x;
            float outerCos = lightConeCos[i].y;
            spotFactor = smoothstep(outerCos, innerCos, cosTheta);
        }

        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        if(NdotL <= 0.0) continue;

        vec3 radiance = lightColor[i] * lightIntensity[i] * attenuation * spotFactor * PI;

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, rough);
        float G   = GeometrySmith(N, V, L, rough);
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001; // + 0.0001 to prevent divide by zero
        vec3 specular     = numerator / denominator;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    vec3 emission = getEmission();

    vec3 finalColor = ambient + Lo + emission;

    // Simple tonemapping + gamma for display. This improves night readability and prevents blowouts.
    const float exposure = 1.15;
    vec3 mapped = vec3(1.0) - exp(-max(finalColor, vec3(0.0)) * exposure);
    mapped = linearToSrgb(mapped);

    frag_color = vec4(mapped, tint.a * albedoAlpha);
}
