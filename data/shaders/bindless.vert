#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out vec3 out_world_pos;
layout(location = 3) flat out uint out_material_index;

#include "common.glsl"

layout(set = 0, binding = 0) uniform UBO {
    SceneUBO scene;
};

layout(scalar, set = 1, binding = 0) readonly buffer MeshDrawCommands {
    uint culled_count;
    MeshDrawCommand cmds[];
};

layout(scalar, set = 1, binding = 1) readonly buffer MeshBuffer {
    MeshInstance draw_calls[];
};

layout(scalar, set = 2, binding = 0) readonly buffer IndexBuffer {
    uint indices[];
};

layout(scalar, set = 2, binding = 1) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(scalar, set = 2, binding = 2) readonly buffer MeshletBuffer {
    Meshlet meshlets[];
};

layout(scalar, set = 2, binding = 3) readonly buffer MeshletBoundsBuffer {
    MeshletBounds bounds[];
};

layout(scalar, set = 2, binding = 4) readonly buffer MeshletVertexIndices {
    uint meshlet_vertex_indices[];
};

layout(scalar, set = 2, binding = 5) readonly buffer MeshletPrimitiveIndices {
    uint8_t meshlet_primitive_indices[];
};

void main() {
    MeshInstance draw = draw_calls[gl_BaseInstance];

    uint base = gl_VertexIndex;
    Vertex vertex = vertices[base];

    vec3 world_pos = rotate_quat(vertex.position, draw.rotation) * draw.scale + draw.position;

    vec4 clip_pos = scene.view_proj * vec4(world_pos, 1.0);
    vec3 meshlet_color = vec3(1.0);

    gl_Position = clip_pos;
    out_normal = rotate_quat(vertex.normal, draw.rotation);
    out_uv = vertex.uv;
    out_world_pos = world_pos.xyz;
    out_material_index = draw.material_id;
}
