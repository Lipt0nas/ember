#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "common.glsl"

layout(set = 0, binding = 0) uniform UBO {
    SceneUBO scene;
};

layout(scalar, set = 1, binding = 0) readonly buffer PositionBuffer {
    vec3 positions[];
};

const vec2 offsets[6] = {
        vec2(0.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0),
    };

void main() {
    uint particle_index = gl_VertexIndex / 6;
    uint vertex_index = gl_VertexIndex % 6;

    vec2 offset = offsets[vertex_index];

    vec3 world_pos = positions[particle_index] + vec3(offset * 0.2, 0.0);

    gl_Position = scene.view_proj * vec4(world_pos, 1.0);
}
