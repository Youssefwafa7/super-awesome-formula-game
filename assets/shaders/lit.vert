#version 330 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 tex_coord;
layout(location = 3) in vec3 normal;

uniform mat4 transform; // MVP
uniform mat4 model;     // Model->World

out VS_OUT {
    vec4 color;
    vec2 tex_coord;
    vec3 world_pos;
    vec3 world_normal;
} vs_out;

void main(){
    vec4 worldPos4 = model * vec4(position, 1.0);
    vs_out.world_pos = worldPos4.xyz;

    mat3 normalMat = mat3(transpose(inverse(model)));
    vs_out.world_normal = normalize(normalMat * normal);

    vs_out.color = color;
    vs_out.tex_coord = tex_coord;

    gl_Position = transform * vec4(position, 1.0);
}
