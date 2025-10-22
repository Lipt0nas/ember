#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out vec3 out_meshlet_color;
layout(location = 3) flat out uint out_albedo_index;
layout(location = 4) flat out uint out_normals_index;
layout(location = 5) flat out uint out_material_index;
layout(location = 6) out vec3 out_world_pos;

#include "common.glsl"

layout(set = 0, binding = 0) uniform UBO {
    SceneUBO scene;
};

layout(scalar, set = 1, binding = 0) readonly buffer DrawDataBuffer {
    DrawData draw_data[];
} uniforms;

layout(scalar, set = 1, binding = 1) readonly buffer MeshDrawCommands {
    uint culled_count;
    MeshDrawCommand cmds[];
} draw_commands;

layout(scalar, set = 2, binding = 0) readonly buffer IndexBuffer {
    uint indices[];
} global_indices;

layout(scalar, set = 2, binding = 1) readonly buffer VertexBuffer {
    Vertex vertices[];
} global_vertices;

layout(scalar, set = 2, binding = 2) readonly buffer MeshletBuffer {
    Meshlet meshlets[];
} meshlets;

layout(scalar, set = 2, binding = 3) readonly buffer MeshletBoundsBuffer {
    MeshletBounds bounds[];
} meshlet_bounds;

layout(scalar, set = 2, binding = 4) readonly buffer MeshletVertexIndices {
    uint indices[];
} meshlet_vertex_indices;

layout(scalar, set = 2, binding = 5) readonly buffer MeshletPrimitiveIndices {
    uint8_t indices[];
} meshlet_primitive_indices;

void main() {
    DrawData draw = uniforms.draw_data[gl_BaseInstance];

    uint base = gl_VertexIndex;
    Vertex vertex = global_vertices.vertices[base];

    vec3 world_pos = rotate_quat(vertex.position, draw.rotation) * draw.scale + draw.position;

    vec4 clip_pos = scene.view_proj * vec4(world_pos, 1.0);
    vec3 meshlet_color = vec3(1.0);

    gl_Position = clip_pos;
    out_normal = rotate_quat(vertex.normal, draw.rotation);
    out_uv = vertex.uv;
    out_meshlet_color = meshlet_color;
    out_albedo_index = draw.albedo_index;
    out_normals_index = draw.normals_index;
    out_material_index = draw.material_index;
    out_world_pos = world_pos.xyz;
}
