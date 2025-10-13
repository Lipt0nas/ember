#define MESHLETS_PER_TASK 32

struct DrawData {
    mat4 model;
    mat4 normal_matrix;

    vec3 center;
    float radius;

    uint index_count;
    uint first_index;
    int vertex_offset;

    uint meshlet_offset;
    uint meshlet_count;

    uint albedo_index;
    uint normals_index;
    uint material_index;
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
    mat4 view;
    mat4 proj;

    mat4 view_proj;
    mat4 inverse_view_proj;
    vec4 planes[6];
    vec4 camera_position;

    mat4 frozen_view_proj;
    vec4 frozen_planes[6];
    vec4 frozen_camera_position;

    uint debug_frustum;
    uint disable_culling;

    float P00;
    float P11;
} scene;

bool is_sphere_in_frustum(vec3 world_center, float world_radius, vec4[6] planes) {
    for (int i = 0; i < 6; i++) {
        if (dot(world_center, planes[i].xyz) + planes[i].w <= -world_radius) {
            return false;
        }
    }

    return true;
}
