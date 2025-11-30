#define MESHLETS_PER_TASK 32

#define MAP_METALLIC(input) (1.0 - input)

#define EFFECT_RADIUS 0.75
#define EFFECT_FALLOFF_RANGE 0.615

#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER           1.457       // Allows us to use different value as compared to ground truth radius to counter inherent screen space biases
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER   2.0         // Distribution of samples around the hemisphere (higher = more focused around normal)
#define XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION  0.0         // Reduces artifacts from thin occluders (0 = off, higher = more compensation)
#define XE_GTAO_DEFAULT_FALLOFF_RANGE               0.615       // Distance falloff range as percentage of effectRadius (0.615 is optimal)
#define XE_GTAO_DEFAULT_FINAL_VALUE_POWER           2.2         // Power curve for final AO value (for aesthetics)
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET   3.30        // Controls which MIP level to use based on sample distance

#define XE_GTAO_DEFAULT_SLICE_COUNT                 6           // Number of slices around the hemisphere (2-9 typical, 3 is good balance)
#define XE_GTAO_DEFAULT_STEPS_PER_SLICE             3           // Steps per slice (2-6 typical, 3 is good balance)

#define XE_GTAO_DEPTH_MIP_LEVELS 5
#define XE_GTAO_MIP_SAMPLING_OFFSET 3.3

#define XE_GTAO_PI               	(3.1415926535897932384626433832795)
#define XE_GTAO_PI_HALF             (1.5707963267948966192313216916398)

#define XE_GTAO_OCCLUSION_TERM_SCALE                    1.0

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
    uint occlusion_index;

    vec3 emission_color;
    uint emissive_index;
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

const vec2 inverse_atan = vec2(0.1591, 0.3183);
vec2 spherical_uv(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= inverse_atan;
    uv += 0.5;

    return uv;
}

vec3 reconstruct_world_position(vec2 uv, float depth, mat4 inverse_view_proj) {
    vec4 ndc = vec4(uv.x * 2 - 1, 1.0 - uv.y * 2, depth, 1.0);
    vec4 world_pos = inverse_view_proj * ndc;

    return world_pos.xyz / world_pos.w;
}

vec2 oct_encode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 p = n.xy;
    if (n.z < 0.0) {
        p = (1.0 - abs(p.yx)) * sign(p);
    }

    return p * 0.5 + 0.5;
}

vec3 oct_decode(vec2 oct) {
    oct = oct * 2.0 - 1.0;
    vec3 n = vec3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0) {
        vec2 signNotZero = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }

    return normalize(n);
}

ivec2 probe_index_to_atlas_coord(ivec3 probe_coords, ivec3 probe_counts, int probes_per_row) {
    int linear_index = probe_coords.x +
            probe_coords.z * probe_counts.x +
            probe_coords.y * probe_counts.x * probe_counts.z;

    return ivec2(linear_index % probes_per_row, linear_index / probes_per_row);
}

vec3 get_probe_world_position(ivec3 probe_coords, vec3 grid_origin, float probe_spacing) {
    return grid_origin + vec3(probe_coords) * probe_spacing;
}

float gradient_noise(vec2 uv) {
    return fract(52.9829189 * fract(dot(uv, vec2(0.06711056, 0.00583715))));
}
