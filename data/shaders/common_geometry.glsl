#include "common.glsl"

layout(scalar, set = 0, binding = 2) readonly buffer DrawDataBuffer {
    DrawData draw_data[];
} uniforms;

layout(scalar, set = 0, binding = 3) readonly buffer IndexBuffer {
    uint indices[];
} global_indices;

layout(scalar, set = 0, binding = 4) readonly buffer VertexBuffer {
    Vertex vertices[];
} global_vertices;

layout(scalar, set = 0, binding = 5) readonly buffer MeshletBuffer {
    Meshlet meshlets[];
} meshlets;

layout(scalar, set = 0, binding = 6) readonly buffer MeshletBoundsBuffer {
    MeshletBounds bounds[];
} meshlet_bounds;

layout(scalar, set = 0, binding = 7) readonly buffer MeshletVertexIndices {
    uint indices[];
} meshlet_vertex_indices;

layout(scalar, set = 0, binding = 8) readonly buffer MeshletPrimitiveIndices {
    uint8_t indices[];
} meshlet_primitive_indices;

struct TaskPayload {
    uint draw_id;
    uint meshlet_offset;
    uint meshlet_indices[MESHLETS_PER_TASK];
};
