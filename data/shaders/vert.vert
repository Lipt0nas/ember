#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec2 out_uv;

layout(push_constant, std430) uniform pc {
    mat4 transform;
    mat4 model;
} push_constants;

void main() {
    out_color = in_normal;
    out_uv = in_uv;

    gl_Position = push_constants.transform * push_constants.model * vec4(in_position, 1.0);
}
