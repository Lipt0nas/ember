#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#define MESHLETS_PER_TASK 32

#define EFFECT_RADIUS 0.5
#define EFFECT_FALLOFF_RANGE 0.615

#define XE_GTAO_DEFAULT_RADIUS_MULTIPLIER           1.457       // Allows us to use different value as compared to ground truth radius to counter inherent screen space biases
#define XE_GTAO_DEFAULT_SAMPLE_DISTRIBUTION_POWER   2.0         // Distribution of samples around the hemisphere (higher = more focused around normal)
#define XE_GTAO_DEFAULT_THIN_OCCLUDER_COMPENSATION  0.0         // Reduces artifacts from thin occluders (0 = off, higher = more compensation)
#define XE_GTAO_DEFAULT_FALLOFF_RANGE               0.615       // Distance falloff range as percentage of effectRadius (0.615 is optimal)
#define XE_GTAO_DEFAULT_FINAL_VALUE_POWER           1.2         // Power curve for final AO value (for aesthetics)
#define XE_GTAO_DEFAULT_DEPTH_MIP_SAMPLING_OFFSET   3.30        // Controls which MIP level to use based on sample distance

// 0 - low
// 1 - medium
// 2 - high
// 3 - ultra
#define XEGTAO_QUALITY 2

#if XEGTAO_QUALITY == 0
#define XE_GTAO_DEFAULT_SLICE_COUNT                 1
#define XE_GTAO_DEFAULT_STEPS_PER_SLICE             2
#elif XEGTAO_QUALITY == 1
#define XE_GTAO_DEFAULT_SLICE_COUNT                 2
#define XE_GTAO_DEFAULT_STEPS_PER_SLICE             2
#elif XEGTAO_QUALITY == 2
#define XE_GTAO_DEFAULT_SLICE_COUNT                 3
#define XE_GTAO_DEFAULT_STEPS_PER_SLICE             3
#elif XEGTAO_QUALITY == 3
#define XE_GTAO_DEFAULT_SLICE_COUNT                 9
#define XE_GTAO_DEFAULT_STEPS_PER_SLICE             3
#endif

#define XE_GTAO_DEPTH_MIP_LEVELS 5
#define XE_GTAO_MIP_SAMPLING_OFFSET 3.3

#define PI2                 (6.2831853071795864)
#define PI               	(3.1415926535897932384626433832795)
#define PI_HALF             (1.5707963267948966192313216916398)

#define XE_GTAO_OCCLUSION_TERM_SCALE                    1.5

struct IndirectDispatchCommand {
    uint workgroups_x;
    uint workgroups_y;
    uint workgroups_z;
};

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

    uint object_id;
};

struct MeshDrawCommand {
    uint group_count_x;
    uint group_count_y;
    uint group_count_z;

    uint object_id;
    uint meshlet_count;
    uint meshlet_offset;
};

struct Material {
    uint albedo_index;
    uint normals_index;
    uint material_index;
    uint emissive_index;

    vec4 albedo_factor;
    vec3 emissive_factor;

    float roughness_factor;
    float metallic_factor;
    float normal_scale;
};

struct MeshLOD {
    uint index_offset;
    uint index_count;

    uint meshlet_offset;
    uint meshlet_count;

    float error;
};

struct Mesh {
    vec3 center;
    float radius;

    vec4 bounds_min;
    vec4 bounds_max;

    uint vertex_offset;
    uint vertex_count;

    uint lod_count;
    MeshLOD lods[8];
};

struct MeshInstance {
    int mesh_id;
    int material_id;

    vec3 position;
    float scale;

    vec4 rotation;

    vec3 last_position;
    float last_scale;

    vec4 last_rotation;
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

    int use_probe_state;
    int use_bent_normals;
    int compensate_specular;
    int disney_diffuse;

    vec4 sky_hemisphere_top;
    vec4 sky_hemisphere_bottom;

    vec4 ddgi_probe_ray_rotation;
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
    float16_t px;
    float16_t py;
    float16_t pz;

    float16_t ux;
    float16_t uy;

    uint16_t tn;

    // normal + tangent sign
    uint norm;
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
    float target_lod;
    float _pad;

    mat4 last_frame_view_proj;
};

vec4 material_get_albedo(const Material material, sampler2D albedo_sampler, vec2 uv) {
    vec4 albedo = material.albedo_factor;
    if (material.albedo_index > 0) {
        albedo *= texture(albedo_sampler, uv);
    }

    return albedo;
}

vec3 material_get_normal(const Material material, sampler2D normal_sampler, vec2 uv) {
    vec3 normal = vec3(0, 0, 1.0);

    if (material.normals_index > 0) {
        normal = normalize(texture(normal_sampler, uv).rgb * 2.0 - 1.0);
    }

    return normal * vec3(material.normal_scale, material.normal_scale, 1.0);
}

vec3 material_get_emissive(const Material material, sampler2D emissive_sampler, vec2 uv) {
    vec3 emissive = material.emissive_factor;

    if (material.emissive_index > 0) {
        emissive *= texture(emissive_sampler, uv).rgb;
    }

    return emissive;
}

vec2 material_get_roughness_metallic(const Material material, sampler2D material_sampler, vec2 uv) {
    vec2 roughness_metallic = vec2(material.roughness_factor, material.metallic_factor);

    if (material.material_index > 0) {
        roughness_metallic *= texture(material_sampler, uv).yz;
    }

    roughness_metallic.x = clamp(roughness_metallic.x, 0.05, 1.0);

    // roughness_metallic = vec2(1.0, 0.0);

    return roughness_metallic;
}

vec4 conjugate_quat(vec4 q) {
    return vec4(-q.xyz, q.w);
}

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
vec3 get_sky_color(vec3 ray_dir, vec3 sun_dir, vec4 hemisphere_top, vec4 hemisphere_bottom, vec4 sun_params) {
    vec3 rd = normalize(ray_dir);
    vec3 sd = normalize(sun_dir);

    float sky_gradient = rd.y * 0.5 + 0.5;
    float atmosphere = sqrt(max(0.0, rd.y));

    float sun_height = sd.y * 0.5 + 0.5;
    float scatter = pow(sun_height, 1.0 / 15.0);
    scatter = 1.0 - clamp(scatter, 0.8, 1.0);

    vec3 base_sky = mix(hemisphere_bottom.rgb, hemisphere_top.rgb, sky_gradient);
    vec3 scatter_color = mix(base_sky, sun_params.rgb * 1.5 * sun_params.w, scatter);
    vec3 sky_color = mix(base_sky, scatter_color, atmosphere / 1.3);

    float sun_dot = dot(rd, sd);
    float sun_disk = clamp(sun_dot, 0.0, 1.0);

    float sun = pow(sun_disk, 1000.0);
    sun = clamp(sun, 0.0, 1.0);

    float glow = pow(sun_disk, 50.0) * 0.1;
    sun += glow;

    float height_factor = pow(max(0.0, rd.y), 1.0 / 1.65);
    sun *= height_factor;

    vec3 sun_color = sun_params.rgb * sun * sun_params.w;

    return sky_color + sun_color;
}

vec2 pack_normals(vec3 normals) {
    return oct_encode(normals);
}

// Assuming normals are stored in the xy and channel
vec3 unpack_normals(vec4 data) {
    return oct_decode(data.xy);
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

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

vec3 F_Schlick_Roughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 F_Schlick(float u, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - u, 5.0);
}

float F_Schlick(float u, float f0, float f90) {
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

float V_Smith_GGX_Correlated(float NoV, float NoL, float a) {
    float a2 = a * a;

    NoV = max(NoV, 1e-5);
    NoL = max(NoL, 1e-5);

    float GGXL = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float GGXV = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return 0.5 / (GGXV + GGXL);
}

vec3 D_Lambert(vec3 albedo) {
    return albedo / PI;
}

float D_Disney(float NoV, float NoL, float LoH, float roughness) {
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float light_scatter = F_Schlick(NoL, 1.0, f90);
    float view_scatter = F_Schlick(NoV, 1.0, f90);

    return light_scatter * view_scatter * (1.0 / PI);
}

float D_Oren_Nayar(float NdotV, float NdotL, float a, vec3 L, vec3 V) {
    float A = 1.0 - 0.5 * (a / (a + 0.57));
    float B = 0.45 * (a / (a + 0.09));

    float s = dot(L, V) - NdotL * NdotV;
    float t = mix(1.0, max(NdotL, NdotV), step(0.0, s));

    return (A + B * max(0.0, s) / t) / PI;
}

float component_max(vec3 values) {
    return max(values.x, max(values.y, values.z));
}

float linear_rgb_to_luminance(vec3 rgb) {
    const vec3 weights = vec3(0.2126, 0.7152, 0.0722);

    return dot(rgb, weights);
}

vec3 unpack_vertex_normal(uint packed) {
    return vec3(
        (packed & 1023) / 511.0 - 1.0,
        ((packed >> 10) & 1023) / 511.0 - 1.0,
        ((packed >> 20) & 1023) / 511.0 - 1.0
    );
}

float unpack_tangent_sign(uint packed) {
    return (packed & (1 << 30)) != 0 ? -1.0 : 1.0;
}

vec3 unpack_tangent(uint packed) {
    return oct_decode(vec2((packed & 255) / 127.0 - 1.0, ((packed >> 8) & 255) / 127.0 - 1.0));
}

#endif
