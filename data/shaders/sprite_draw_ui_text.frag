#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) flat in int in_data_index;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main() {
    float distance = texture(textures[nonuniformEXT(in_data_index)], in_uv).r;
    float alpha = smoothstep(0.5 - 0.1, 0.5 + 0.1, distance);

    out_color = vec4(in_color * alpha);
}
