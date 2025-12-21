#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_pos;
layout(location = 1) out vec3 out_normal;

layout(scalar, set = 0, binding = 2) readonly buffer DrawDataBuffer {
    vec3 draw_data[];
};

layout(push_constant, std430) uniform pc {
    mat4 combined_matrix;
    vec3 camera_pos;
} push;

void main() {
    vec4 pos = vec4(in_position + draw_data[gl_InstanceIndex], 1.0);
    gl_Position = push.combined_matrix * pos;

    out_pos = pos.xyz;
    out_normal = in_normal;
}
