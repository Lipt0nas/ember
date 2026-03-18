#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

layout(location = 0) in vec3 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(in_color, 1.0);
}
