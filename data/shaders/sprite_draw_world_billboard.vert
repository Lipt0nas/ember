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
layout(location = 5) in uint in_drawcall_index;

layout(location = 0) out vec3 out_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_color;
layout(location = 4) flat out int out_data_index;

layout(scalar, set = 1, binding = 0) readonly buffer SpriteBuffer {
    SpriteDraw draw_calls[];
};

layout(push_constant, std430) uniform pc {
    mat4 combined_matrix;
    mat4 view_matrix;
} push;

void main() {
    vec3 cam_right = -vec3(push.view_matrix[0][0], push.view_matrix[1][0], push.view_matrix[2][0]);
    vec3 cam_up = vec3(push.view_matrix[0][1], push.view_matrix[1][1], push.view_matrix[2][1]);

    vec3 offset = in_position - draw_calls[in_drawcall_index].position;
    vec3 pos = draw_calls[in_drawcall_index].position + cam_right * offset.x + cam_up * offset.y;

    gl_Position = push.combined_matrix * vec4(pos, 1.0);

    out_pos = in_position;
    out_normal = in_normal;
    out_uv = in_uv;
    out_color = in_color;
    out_data_index = in_data_index;
}
