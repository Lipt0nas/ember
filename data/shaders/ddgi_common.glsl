#ifndef DDGI_COMMON_GLSL
#define DDGI_COMMON_GLSL

#include "common.glsl"

struct DDGIRay {
    vec4 ray_data; // xyz - irradiance, w - distance
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

vec3 sample_ddgi(LightingUBO lighting, vec3 surface_pos, vec3 surface_normal, vec3 view_dir, sampler2D ddgi_atlas, sampler2D ddgi_depth) {
    float normal_bias = 0.2;
    float view_bias = 0.8;
    float axial_bias = 0.75;

    vec3 biased_pos = surface_pos + ((surface_normal * normal_bias + view_dir * view_bias) * axial_bias * lighting.probe_spacing * 0.2);

    vec3 grid_pos_float = (biased_pos - lighting.grid_origin) / lighting.probe_spacing;
    ivec3 base_grid_coord = ivec3(floor(grid_pos_float));
    vec3 alpha = clamp(grid_pos_float - vec3(base_grid_coord), vec3(0.0), vec3(1.0));

    vec3 total_irradiance = vec3(0.0);
    float total_weight = 0.0;

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3((i >> 0) & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 probe_grid_coord = clamp(base_grid_coord + offset, ivec3(0), lighting.probe_counts - ivec3(1));

        int probe_idx = probe_grid_coord.z * (lighting.probe_counts.x * lighting.probe_counts.y)
                + probe_grid_coord.y * lighting.probe_counts.x
                + probe_grid_coord.x;

        vec3 probe_pos = lighting.grid_origin + vec3(probe_grid_coord) * lighting.probe_spacing;

        // Direction from surface to probe (for directional weight)
        vec3 surface_to_probe = normalize(probe_pos - surface_pos);

        // Direction from probe to surface (for visibility test)
        vec3 probe_to_surface = normalize(biased_pos - probe_pos);
        float dist_to_probe = length(biased_pos - probe_pos);

        // Trilinear weight
        vec3 trilinear = max(vec3(0.001), mix(1.0 - alpha, alpha, vec3(offset)));
        float weight = 1.0;

        // Directional weight (wrap shading)
        float wrap_shading = max(0.0001, (dot(surface_to_probe, surface_normal) + 1.0) * 0.5);
        weight *= (wrap_shading * wrap_shading) + 0.2;

        // Chebyshev visibility test
        // Sample depth at probe-to-surface direction
        vec2 depth_data = texture(ddgi_depth, ddgi_probe_uv(lighting.probe_counts, probe_idx, probe_to_surface, lighting.depth_texels_per_probe)).rg;
        float mean_dist = depth_data.x;
        float mean_dist_sq = depth_data.y;

        float variance = abs((mean_dist * mean_dist) - mean_dist_sq);

        float chebyshev = 1.0;
        if (dist_to_probe > mean_dist) {
            float v = dist_to_probe - mean_dist;
            chebyshev = variance / (variance + (v * v));
            chebyshev = max(pow(chebyshev, 3.0), 0.0);
        }

        if (lighting.remove_visiblity_checks == 1) {
            chebyshev = 1.0;
        }

        weight *= max(0.05, chebyshev);
        weight = max(0.000001, weight);

        // Crush small weights
        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= (weight * weight) * (1.0 / (crushThreshold * crushThreshold));
        }
        weight *= trilinear.x * trilinear.y * trilinear.z;

        // Sample irradiance at surface normal direction
        vec3 probe_irradiance = texture(ddgi_atlas, ddgi_probe_uv(lighting.probe_counts, probe_idx, surface_normal, lighting.texels_per_probe)).rgb;

        vec3 exponent = vec3(5.0 * 0.5);
        probe_irradiance = pow(probe_irradiance, exponent);

        total_irradiance += weight * probe_irradiance;
        total_weight += weight;
    }

    if (total_weight == 0.0) return vec3(0.0);

    total_irradiance /= total_weight;
    total_irradiance *= total_irradiance; // Square back to linear
    // total_irradiance *= 2.0 * 3.14159265; // 2π for hemisphere integration

    return total_irradiance;
}

#endif
