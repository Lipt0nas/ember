#ifndef DDGI_COMMON_GLSL
#define DDGI_COMMON_GLSL

#include "common.glsl"

#define DDGI_MAX_RAY_COUNT 256
#define DDGI_NUM_FIXED_RAYS 32

#define DDGI_PROBE_NUM_DEPTH_TEXELS 16
#define DDGI_PROBE_NUM_DEPTH_INTERIOR_TEXELS 14

#define DDGI_PROBE_NUM_RADIANCE_TEXELS 8
#define DDGI_PROBE_NUM_RADIANCE_INTERIOR_TEXELS 6

#define DDGI_MAX_PROBE_COUNTS ivec3(32, 16, 32)

struct DDGIRay {
    vec4 ray_data; // xyz - irradiance, w - distance
};

struct DDGIProbe {
    vec3 offset;
    int state;
};

struct DDGIVolume {
    vec3 grid_origin;
    int enabled;

    ivec3 probe_counts;
    float intensity;

    vec3 probe_spacing;
    int rays_per_probe;

    float hysteresis;
    float normal_bias;
    float view_bias;
    float distance_exponent;

    float irradiance_encoding_gamma;
    float irradiance_threshold;
    float brightness_threshold;
    float random_ray_backface_threshold;

    float fixed_ray_backface_threshold;
    float min_frontface_distance;
    int scrolling;
    float distance_scale;

    vec4 rotation;

    ivec3 scroll_offsets;
    float _pad1;
    ivec4 scroll_clear;
    ivec4 scroll_directions;

    vec4 probe_ray_rotation;
};

ivec3 ddgi_scrolled_coord(ivec3 grid_coord, ivec3 probe_counts, ivec3 scroll_offsets) {
    return (grid_coord + scroll_offsets + probe_counts) % probe_counts;
}

vec3 ddgi_get_effective_origin(DDGIVolume volume) {
    if (volume.scrolling == 1) {
        return volume.grid_origin + vec3(volume.scroll_offsets) * volume.probe_spacing;
    }

    return volume.grid_origin;
}

vec3 ddgi_get_probe_position(int index, ivec3 grid_dims, DDGIVolume volume, vec3 probe_offset) {
    ivec3 pos;
    pos.x = index % grid_dims.x;
    pos.y = (index / grid_dims.x) % grid_dims.y;
    pos.z = index / (grid_dims.x * grid_dims.y);

    vec3 probe_grid_position = vec3(pos) * volume.probe_spacing;
    vec3 grid_shift = (volume.probe_spacing * vec3(grid_dims - ivec3(1))) * 0.5;
    vec3 probe_world_position = probe_grid_position - grid_shift;

    vec3 origin = volume.grid_origin;
    if (volume.scrolling == 1) {
        origin += vec3(volume.scroll_offsets) * volume.probe_spacing;
    }

    return origin + probe_world_position + probe_offset;
}

ivec3 ddgi_probe_coords(int probe_index, DDGIVolume volume) {
    int actual_per_row = volume.probe_counts.x * volume.probe_counts.y;

    return ivec3(
        probe_index % volume.probe_counts.x,
        (probe_index % actual_per_row) / volume.probe_counts.x,
        probe_index / actual_per_row
    );
}

int ddgi_probe_index(ivec3 coords, DDGIVolume volume) {
    return coords.z * (volume.probe_counts.x * volume.probe_counts.y)
        + coords.y * volume.probe_counts.x
        + coords.x;
}

int ddgi_physical_probe_index(int logical_index, DDGIVolume volume) {
    if (volume.scrolling == 0) return logical_index;

    ivec3 coords = ddgi_probe_coords(logical_index, volume);
    ivec3 scrolled = ddgi_scrolled_coord(coords, volume.probe_counts, volume.scroll_offsets);

    return ddgi_probe_index(scrolled, volume);
}

float ddgi_get_volume_blend_weight(vec3 world_pos, DDGIVolume volume) {
    vec3 origin = ddgi_get_effective_origin(volume);
    vec3 extent = (volume.probe_spacing * (volume.probe_counts - 1)) * 0.5;

    vec3 position = abs(world_pos - origin);

    vec3 delta = position - extent;
    if (all(lessThan(delta, vec3(0.0)))) {
        return 1.0;
    }

    float weight = 1.0;
    weight *= (1.0 - clamp(delta.x / volume.probe_spacing.x, 0.0, 1.0));
    weight *= (1.0 - clamp(delta.y / volume.probe_spacing.y, 0.0, 1.0));
    weight *= (1.0 - clamp(delta.z / volume.probe_spacing.z, 0.0, 1.0));

    return weight;
}

vec3 spherical_fibonacci(float sample_index, float num_samples) {
    const float b = (sqrt(5.0) * 0.5 + 0.5) - 1.0;
    float phi = (PI * 2) * fract(sample_index * b);
    float cos_theta = 1.0 - (2.0 * sample_index + 1.0) * (1.0 / num_samples);
    float sin_theta = sqrt(clamp(1.0 - (cos_theta * cos_theta), 0.0, 1.0));

    return vec3((cos(phi) * sin_theta), (sin(phi) * sin_theta), cos_theta);
}

vec3 ddgi_get_probe_ray_direction(int ray_index, int ray_count, vec4 random_rotation) {
    bool is_fixed_ray = (ray_index < DDGI_NUM_FIXED_RAYS);
    int sample_index = is_fixed_ray ? ray_index : (ray_index - DDGI_NUM_FIXED_RAYS);
    int num_rays = is_fixed_ray ? DDGI_NUM_FIXED_RAYS : (ray_count - DDGI_NUM_FIXED_RAYS);

    vec3 direction = spherical_fibonacci(sample_index, num_rays);

    if (is_fixed_ray) {
        return normalize(direction);
    }

    return normalize(rotate_quat(direction, conjugate_quat(random_rotation)));
}

vec2 ddgi_probe_uv(DDGIVolume volume, int probe_index, vec3 dir, int texel_count) {
    int max_probes_per_row = DDGI_MAX_PROBE_COUNTS.x * DDGI_MAX_PROBE_COUNTS.y;

    ivec3 grid_coord;
    int actual_per_row = volume.probe_counts.x * volume.probe_counts.y;
    grid_coord.z = probe_index / actual_per_row;
    grid_coord.y = (probe_index % actual_per_row) / volume.probe_counts.x;
    grid_coord.x = probe_index % volume.probe_counts.x;

    int atlas_index = grid_coord.z * max_probes_per_row
            + grid_coord.y * DDGI_MAX_PROBE_COUNTS.x
            + grid_coord.x;

    int probe_x = atlas_index % max_probes_per_row;
    int probe_y = atlas_index / max_probes_per_row;

    float res_raw = float(texel_count);
    float res_full = res_raw + 2.0;

    vec2 oct_uv = oct_encode(normalize(dir));

    float texture_width = float(max_probes_per_row) * res_full;
    float texture_height = float(DDGI_MAX_PROBE_COUNTS.z) * res_full;

    vec2 probe_base_pos = vec2(float(probe_x) * res_full, float(probe_y) * res_full);
    vec2 local_pos = oct_uv * res_raw + 1.0;

    return (probe_base_pos + local_pos) / vec2(texture_width, texture_height);
}

#endif
