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

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};

layout(set = 1, binding = 0) uniform SceneUBO {
    mat4 view_proj;
    mat4 inverse_view_proj;
    vec4 planes[6];
    vec4 camera_position;

    mat4 frozen_view_proj;
    vec4 frozen_planes[6];
    vec4 frozen_camera_position;

    uint debug_frustum;
    uint disable_culling;

    uint _pad0;
    uint _pad1;
} scene;
