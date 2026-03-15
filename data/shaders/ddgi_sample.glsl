#ifndef DDGI_SAMPLE_GLSL
#define DDGI_SAMPLE_GLSL

struct DDGIWeights {
    int probe_indices[8];
    float weights[8];
    float total_weight;
    bool valid;
};

DDGIWeights compute_ddgi_weights(
    LightingUBO lighting,
    vec3 surface_pos,
    vec3 surface_normal,
    vec3 view_dir,
    sampler2D ddgi_depth
) {
    DDGIWeights result;
    result.total_weight = 0.0;
    result.valid = false;

    vec3 biased_pos = surface_pos + (surface_normal * DDGI_PROBE_NORMAL_BIAS)
            + (view_dir * DDGI_PROBE_VIEW_BIAS);

    vec3 grid_shift = vec3(lighting.probe_counts - ivec3(1)) * 0.5;
    vec3 grid_pos_float = ((biased_pos - lighting.grid_origin) / lighting.probe_spacing) + grid_shift;

    ivec3 base_grid_coord = ivec3(floor(grid_pos_float));
    vec3 alpha = clamp(grid_pos_float - vec3(base_grid_coord), vec3(0.0), vec3(1.0));

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3((i >> 0) & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 probe_grid_coord = clamp(base_grid_coord + offset, ivec3(0), lighting.probe_counts - ivec3(1));

        int probe_idx = probe_grid_coord.z * (lighting.probe_counts.x * lighting.probe_counts.y)
                + probe_grid_coord.y * lighting.probe_counts.x
                + probe_grid_coord.x;

        result.probe_indices[i] = probe_idx;

        DDGIProbe probe = probes[probe_idx];
        if (probe.state == 0) {
            result.weights[i] = 0.0;
            continue;
        }

        vec3 probe_pos = ddgi_get_probe_position(
                probe_idx,
                lighting.probe_counts,
                lighting.grid_origin,
                vec3(lighting.probe_spacing),
                probe.offset
            );

        vec3 to_probe_biased = biased_pos - probe_pos;
        float dist_to_probe = length(to_probe_biased);
        vec3 probe_to_surface = to_probe_biased / max(dist_to_probe, 0.0001);

        vec3 surface_to_probe = normalize(probe_pos - surface_pos);

        float weight = 1.0;

        // Directional weight (wrap shading)
        float wrap_shading = max(0.0001, (dot(surface_to_probe, surface_normal) + 1.0) * 0.5);
        weight *= (wrap_shading * wrap_shading) + 0.2;

        // Chebyshev visibility
        vec2 depth_data = 2.0 * texture(ddgi_depth, ddgi_probe_uv(
                        lighting.probe_counts, probe_idx, probe_to_surface,
                        lighting.depth_texels_per_probe)).rg;
        float mean_dist = depth_data.x;
        float mean_dist_sq = depth_data.y;
        float variance = abs(mean_dist * mean_dist - mean_dist_sq);

        if (dist_to_probe > mean_dist) {
            float v = dist_to_probe - mean_dist;
            float chebyshev = variance / (variance + v * v);
            chebyshev = chebyshev * chebyshev * chebyshev; // pow3, no pow()
            weight *= max(chebyshev, 0.05);
        }

        weight = max(weight, 0.000001);

        // Crush small weights
        const float crushThreshold = 0.2;
        if (weight < crushThreshold) {
            weight *= (weight * weight) * (1.0 / (crushThreshold * crushThreshold));
        }

        // Trilinear weight
        vec3 trilinear = max(vec3(0.001), mix(1.0 - alpha, alpha, vec3(offset)));
        weight *= trilinear.x * trilinear.y * trilinear.z;

        result.weights[i] = weight;
        result.total_weight += weight;
        result.valid = true;
    }

    return result;
}

vec3 sample_ddgi(
    DDGIWeights s,
    LightingUBO lighting,
    vec3 direction,
    sampler2D ddgi_atlas
) {
    if (!s.valid || s.total_weight == 0.0) return vec3(0.0);

    vec3 exponent = vec3(DDGI_PROBE_IRRADIANCE_ENCODING_GAMMA * 0.5);
    vec3 total_irradiance = vec3(0.0);

    for (int i = 0; i < 8; i++) {
        if (s.weights[i] == 0.0) continue;

        vec3 probe_irradiance = texture(ddgi_atlas, ddgi_probe_uv(
                    lighting.probe_counts,
                    s.probe_indices[i],
                    direction,
                    lighting.texels_per_probe
                )).rgb;

        probe_irradiance = pow(probe_irradiance, exponent);
        total_irradiance += s.weights[i] * probe_irradiance;
    }

    total_irradiance /= s.total_weight;
    total_irradiance *= total_irradiance;
    total_irradiance *= PI2;

    return total_irradiance;
}
#endif
