#ifndef COMMON_GLSL
#define COMMON_GLSL

#define MESHLETS_PER_TASK 32

#define MAP_METALLIC(input) (input)

#define EFFECT_RADIUS 0.75
#define EFFECT_FALLOFF_RANGE 0.615

#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER           1.457       // Allows us to use different value as compared to ground truth radius to counter inherent screen space biases
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER   2.0         // Distribution of samples around the hemisphere (higher = more focused around normal)
#define XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION  0.0         // Reduces artifacts from thin occluders (0 = off, higher = more compensation)
#define XE_GTAO_DEFAULT_FALLOFF_RANGE               0.615       // Distance falloff range as percentage of effectRadius (0.615 is optimal)
#define XE_GTAO_DEFAULT_FINAL_VALUE_POWER           1.2         // Power curve for final AO value (for aesthetics)
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET   3.30        // Controls which MIP level to use based on sample distance

#define XE_GTAO_DEFAULT_SLICE_COUNT                 6           // Number of slices around the hemisphere (2-9 typical, 3 is good balance)
#define XE_GTAO_DEFAULT_STEPS_PER_SLICE             3           // Steps per slice (2-6 typical, 3 is good balance)

#define XE_GTAO_DEPTH_MIP_LEVELS 5
#define XE_GTAO_MIP_SAMPLING_OFFSET 3.3

#define PI               	(3.1415926535897932384626433832795)
#define PI_HALF             (1.5707963267948966192313216916398)

#define XE_GTAO_OCCLUSION_TERM_SCALE                    1.5

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

struct Material {
    uint albedo_index;
    uint normals_index;
    uint material_index;
    uint occlusion_index;

    vec3 emissive_color;
    uint emissive_index;

    float roughness_multiplier;
    float metallic_multiplier;
};

struct Mesh {
    uint vertex_buffer_offset;
    uint index_buffer_offset;

    uint meshlet_offset;
    uint meshlet_count;

    uint vertex_count;
    uint vertex_offset;

    uint index_count;
    uint first_index;

    vec3 center;
    float radius;
    vec3 bounds_min;
    vec3 bounds_max;
};

struct MeshInstance {
    int mesh_id;
    int material_id;

    vec3 position;
    float scale;

    vec4 rotation;
};

struct LightingUBO {
    vec4 light_direction;
    vec4 light_color;

    vec3 grid_origin;
    float probe_spacing;

    ivec3 probe_counts;
    int texels_per_probe;

    int multibounce;
    int remove_visiblity_checks;

    int depth_texels_per_probe;
    int rays_per_probe;

    vec3 camera_pos;
    int frame_index;

    int ignore_backface_hits;
    int use_bent_normals;
    int indirect_only;
    int ao_only;

    int invert_multibounce_view_dir;
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

    mat4 last_frame_view_proj;
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

float gradient_noise(vec2 uv) {
    return fract(52.9829189 * fract(dot(uv, vec2(0.06711056, 0.00583715))));
}

// Trowbridge-Reitz GGX normal distribution
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// Schlick-GGX geometry function
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

// Smith's method for geometry obstruction
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 getSkyColor(vec3 ray_dir, vec3 sun_dir) {
    float sky = ray_dir.y * 0.5 + 0.5;
    return mix(vec3(0.6, 0.7, 0.9), vec3(0.3, 0.5, 0.8), sky);
}

// Assuming normals are stored in the xy and channel
vec3 unpack_normals(vec4 data) {
    // return oct_decode(data.xy);
    return data.xyz;
}

uint XEGTAO_pack(vec4 unpacked) {
    return ((uint(clamp(unpacked.x, 0.0, 1.0) * 255.0 + 0.5)) |
        (uint(clamp(unpacked.y, 0.0, 1.0) * 255.0 + 0.5) << 8) |
        (uint(clamp(unpacked.z, 0.0, 1.0) * 255.0 + 0.5) << 16) |
        (uint(clamp(unpacked.w, 0.0, 1.0) * 255.0 + 0.5) << 24));
}

vec4 XEGTAO_unpack(uint packed) {
    vec4 unpacked;
    unpacked.x = float(packed & 0x000000ff) / 255.0;
    unpacked.y = float(((packed >> 8) & 0x000000ff)) / 255.0;
    unpacked.z = float(((packed >> 16) & 0x000000ff)) / 255.0;
    unpacked.w = float(packed >> 24) / 255.0;

    return unpacked;
}

const float e = 2.71828;

float W_f(float x, float e0, float e1) {
    if (x <= e0)
        return 0;
    if (x >= e1)
        return 1;
    float a = (x - e0) / (e1 - e0);
    return a * a * (3 - 2 * a);
}

float H_f(float x, float e0, float e1) {
    if (x <= e0)
        return 0;
    if (x >= e1)
        return 1;
    return (x - e0) / (e1 - e0);
}

float GranTurismoTonemapper(float x) {
    float P = 1;
    float a = 1;
    float m = 0.22;
    float l = 0.4;
    float c = 1.33;
    float b = 0;
    float l0 = (P - m) * l / a;
    float L0 = m - m / a;
    float L1 = m + (1 - m) / a;
    float L_x = m + a * (x - m);
    float T_x = m * pow(x / m, c) + b;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = a * P / (P - S1);
    float S_x = P - (P - S1) * pow(e, -(C2 * (x - S0) / P));
    float w0_x = 1 - W_f(x, 0, m);
    float w2_x = H_f(x, m + l0, m + l0);
    float w1_x = 1 - w0_x - w2_x;
    float f_x = T_x * w0_x + L_x * w1_x + S_x * w2_x;
    return f_x;
}

vec3 aces_film(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

#endif
