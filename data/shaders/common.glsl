#define MESHLETS_PER_TASK 32

struct MeshletTaskPayload {
    uint draw_id;
    uint meshlet_offset;
    uint meshlet_indices[MESHLETS_PER_TASK];
};

struct IndexedDrawCommand {
    uint index_count;
    uint instance_count;
    uint first_index;
    uint vertex_offset;
    uint first_instance;
};

struct MeshDrawCommand {
    uint group_count_x;
    uint group_count_y;
    uint group_count_z;
    uint object_id;
};

struct DrawData {
    vec3 center;
    float radius;

    vec3 position;
    float scale;
    vec4 rotation;

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

struct SceneUBO {
    mat4 proj;
    vec4 camera_position;

    mat4 view_proj;
    mat4 inverse_view_proj;

    mat4 view;
    vec4 frustum;

    mat4 frozen_view;
    vec4 frozen_frustum;

    uint debug_frustum;
    uint disable_culling;

    float P00;
    float P11;

    float near_plane;
    float far_plane;
};

vec3 rotate_quat(vec3 v, vec4 q) {
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

bool project_sphere(vec3 c, float r, float znear, float P00, float P11, out vec4 aabb) {
    if (c.z < r + znear)
        return false;

    vec3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    aabb = vec4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f); // clip space -> uv space

    return true;
}

bool cone_cull(vec3 center, float radius, vec3 cone_axis, float cone_cutoff) {
    return dot(center, cone_axis) >= cone_cutoff * length(center) + radius;
}
