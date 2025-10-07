#define MESHLETS_PER_TASK 32

struct DrawData {
    mat4 model;

    uint first_index;
    int vertex_offset;

    uint meshlet_offset;
    uint meshlet_count;

    uint albedo_index;
    uint normals_index;
    uint material_index;
    float _pad0;
};

struct Meshlet {
    uint vertex_offset;
    uint triangle_offset;
    uint vertex_count;
    uint triangle_count;
};

struct MeshletBounds {
    vec3 center;
    float radius;

    vec3 cone_axis;
    float cone_cutoff;

    vec3 cone_apex;
    float _pad;
};

layout(buffer_reference, scalar) readonly buffer IndexBuffer {
    uint indices[];
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    float data[];
};

layout(buffer_reference, scalar) readonly buffer MeshletBuffer {
    Meshlet meshlets[];
};

layout(buffer_reference, scalar) readonly buffer MeshletBoundsBuffer {
    MeshletBounds bounds[];
};

layout(buffer_reference, scalar) readonly buffer MeshletVertexIndices {
    uint indices[];
};

layout(buffer_reference, scalar) readonly buffer MeshletPrimitiveIndices {
    uint8_t indices[];
};

layout(set = 0, binding = 0) readonly buffer DrawDataBuffer {
    DrawData draw_data[];
} uniforms;

layout(push_constant, std430) uniform pc {
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    MeshletBuffer meshlet_buffer;
    MeshletVertexIndices meshlet_vertex_indices;
    MeshletPrimitiveIndices meshlet_primitive_indices;
    MeshletBoundsBuffer meshlet_bounds_buffer;
} push_constants;

struct TaskPayload {
    uint draw_id;
    uint meshlet_offset;
    uint meshlet_indices[MESHLETS_PER_TASK];
};
