#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

layout(push_constant, std430) uniform pc {
    mat4 combined_matrix;
} push;

void main() {
    vec4 pos = vec4(in_position, 1.0);
    gl_Position = push.combined_matrix * pos;

    out_color = in_color;
}
