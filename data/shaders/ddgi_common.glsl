#ifndef DDGI_COMMON_GLSL
#define DDGI_COMMON_GLSL

#include "common.glsl"

struct DDGIRay {
    vec4 ray_data; // xyz - irradiance, w - distance
};

struct DDGIProbe {
    vec3 offset;
    int state;
};

vec3 ddgi_get_probe_position(int index, ivec3 grid_dims, vec3 origin, vec3 spacing) {
    ivec3 pos;
    pos.x = index % grid_dims.x;
    pos.y = (index / grid_dims.x) % grid_dims.y;
    pos.z = index / (grid_dims.x * grid_dims.y);

    return origin + vec3(pos) * spacing;
}

mat3 ddgi_random_rotation(uint probe_index, uint frame) {
    uint hash = probe_index * 7919u + frame * 2137u;

    float angle_y = float(hash & 0xFFFFu) / 65535.0 * 2.0 * 3.14159265;
    float angle_x = float((hash >> 16) & 0xFFFFu) / 65535.0 * 2.0 * 3.14159265;

    float cy = cos(angle_y), sy = sin(angle_y);
    float cx = cos(angle_x), sx = sin(angle_x);

    return transpose(mat3(
            cy, sy * sx, sy * cx,
            0, cx, -sx,
            -sy, cy * sx, cy * cx
        ));
}

vec3 ddgi_generate_ray_direction(uint ray_index, uint total_rays, uint probe_index, uint frame) {
    float golden_angle = 2.39996322972865;

    float z = 1.0 - 2.0 * (float(ray_index) + 0.5) / float(total_rays);

    float radius = sqrt(1.0 - z * z);

    float phi = float(ray_index) * golden_angle;

    vec3 base_dir = vec3(
            radius * cos(phi),
            radius * sin(phi),
            z
        );

    mat3 rotation = ddgi_random_rotation(probe_index, frame);
    return rotation * base_dir;
}

vec2 ddgi_probe_uv(ivec3 probe_counts, int probe_index, vec3 dir, int texel_count) {
    int probes_per_row = probe_counts.x * probe_counts.y;
    int probe_x = probe_index % probes_per_row;
    int probe_y = probe_index / probes_per_row;

    float res_raw = texel_count;
    float res_full = res_raw + 2.0;

    vec2 oct_uv = oct_encode(normalize(dir));

    float texture_width = float(probes_per_row) * res_full;
    float texture_height = float(probe_counts.z) * res_full;

    vec2 probe_base_pos = vec2(float(probe_x) * res_full, float(probe_y) * res_full);
    vec2 local_pos = (oct_uv * res_raw) + 1.0;

    return (probe_base_pos + local_pos) / vec2(texture_width, texture_height);
}

#endif
