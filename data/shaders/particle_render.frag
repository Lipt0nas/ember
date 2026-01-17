#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "common.glsl"

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(1.0);
}
