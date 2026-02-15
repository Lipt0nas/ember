#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) in int in_data_index;

layout(location = 0) out vec3 out_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_color;
layout(location = 4) flat out int out_data_index;

layout(push_constant, std430) uniform pc {
    mat4 combined_matrix;
} push;

void main() {
    gl_Position = push.combined_matrix * vec4(in_position, 1.0);

    out_pos = in_position;
    out_normal = in_normal;
    out_uv = in_uv;
    out_color = in_color;
    out_data_index = in_data_index;
}
