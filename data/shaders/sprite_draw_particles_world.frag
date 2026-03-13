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

layout(scalar, set = 0, binding = 3) readonly buffer Materials {
    Material materials[];
};

layout(set = 2, binding = 0) uniform sampler2D textures[];

void main() {
    vec4 color = texture(textures[nonuniformEXT(in_data_index)], in_uv);
    color.rgb *= color.a;

    out_color.rgb = color.rgb * in_color.rgb * in_color.a;
    out_color.a = color.a * in_color.a;
}
