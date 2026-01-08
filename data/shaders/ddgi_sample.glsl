#ifndef DDGI_SAMPLE_GLSL
#define DDGI_SAMPLE_GLSL

vec3 sample_ddgi(LightingUBO lighting, vec3 surface_pos, vec3 surface_normal, vec3 view_dir, sampler2D ddgi_atlas, sampler2D ddgi_depth) {
    vec3 biased_pos = surface_pos + ((surface_normal * DDGI_PROBE_NORMAL_BIAS) + (view_dir * DDGI_PROBE_VIEW_BIAS));

    vec3 grid_shift = vec3(lighting.probe_counts - ivec3(1)) * 0.5;
    vec3 grid_pos_float = ((biased_pos - lighting.grid_origin) / lighting.probe_spacing) + grid_shift;

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

        DDGIProbe probe = probes[probe_idx];

        if (probe.state == 0 && lighting.use_probe_state == 1) {
            continue;
        }
        vec3 probe_pos = ddgi_get_probe_position(probe_idx, lighting.probe_counts, lighting.grid_origin, vec3(lighting.probe_spacing), probe.offset);

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

        vec3 exponent = vec3(DDGI_PROBE_IRRADIANCE_ENCODING_GAMMA * 0.5);
        probe_irradiance = pow(probe_irradiance, exponent);

        total_irradiance += weight * probe_irradiance;
        total_weight += weight;
    }

    if (total_weight == 0.0) return vec3(0.0);

    total_irradiance /= total_weight;
    total_irradiance *= total_irradiance;
    // total_irradiance *= 2.0 * 3.14159265;

    return total_irradiance;
}

#endif
