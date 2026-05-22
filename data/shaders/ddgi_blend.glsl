#ifndef DDGI_BLEND
#define DDGI_BLEND

#if !defined(DDGI_PROBE_NUM_TEXELS)
#error DDGI_PROBE_NUM_TEXELS is not defined
#endif

#if !defined(DDGI_PROBE_NUM_INTERIOR_TEXELS)
#error DDGI_PROBE_NUM_INTERIOR_TEXELS is not defined
#endif

#if !defined(DDGI_BLEND_IRRADIANCE) && !defined(DDGI_BLEND_DEPTH)
#error neither DDGI_BLEND_IRRADIANCE nor DDGI_BLEND_DEPTH is defined!
#endif

#if defined(DDGI_BLEND_IRRADIANCE) && defined(DDGI_BLEND_DEPTH)
#error cant have both DDGI_BLEND_IRRADIANCE and DDGI_BLEND_DEPTH defined!
#endif

#if !defined(DDGI_BLEND_RAYS_PER_PROBE)
#error DDGI_BLEND_RAYS_PER_PROBE is not defined!
#endif

layout(local_size_x = DDGI_PROBE_NUM_TEXELS, local_size_y = DDGI_PROBE_NUM_TEXELS, local_size_z = 1) in;

layout(set = 0, binding = 0, std430) readonly buffer RayBuffer {
    DDGIRay rays[];
} ray_buffer;

layout(set = 0, binding = 1, std430) buffer ProbeBuffer {
    DDGIProbe probes[];
};

layout(set = 0, binding = 2, std430) readonly uniform LightingData {
    LightingUBO lighting;
};

#if defined(DDGI_BLEND_IRRADIANCE)
layout(set = 0, binding = 3, rgba32f) uniform image2D out_irradiance;
layout(set = 0, binding = 4, rgba32f) uniform image2D irradiance_history;
#endif

#if defined(DDGI_BLEND_DEPTH)
layout(set = 0, binding = 3, rg16f) uniform image2D out_depth;
layout(set = 0, binding = 4, rg16f) uniform image2D depth_history;
#endif

layout(set = 0, binding = 5, std430) readonly buffer DDGIVolumeData {
    DDGIVolume volume;
};

#if defined(DDGI_BLEND_IRRADIANCE)
shared vec3 shared_ray_irradiance[DDGI_BLEND_RAYS_PER_PROBE];
#endif
shared float shared_ray_distance[DDGI_BLEND_RAYS_PER_PROBE];
shared vec3 shared_ray_direction[DDGI_BLEND_RAYS_PER_PROBE];

void load_shared_memory(int probe_index, uint group_index) {
    int total_iterations = int(ceil(float(DDGI_BLEND_RAYS_PER_PROBE) / float(DDGI_PROBE_NUM_TEXELS * DDGI_PROBE_NUM_TEXELS)));
    for (int i = 0; i < total_iterations; i++) {
        uint ray_index = (group_index * total_iterations) + i;

        if (ray_index >= DDGI_BLEND_RAYS_PER_PROBE) {
            break;
        }

        uint global_ray_index = (probe_index * volume.rays_per_probe) + ray_index;

        #if defined(DDGI_BLEND_IRRADIANCE)
        shared_ray_irradiance[ray_index] = ray_buffer.rays[global_ray_index].ray_data.xyz;
        #endif

        shared_ray_distance[ray_index] = ray_buffer.rays[global_ray_index].ray_data.w;
        shared_ray_direction[ray_index] = ddgi_get_probe_ray_direction(int(ray_index), int(volume.rays_per_probe), volume.probe_ray_rotation);
    }

    barrier();
}

void update_border_texel(uvec3 group_thread_id, uvec3 group_id, uint probe_idx, uint probes_per_row) {
    bool is_corner_texel = (group_thread_id.x == 0 || group_thread_id.x == (DDGI_PROBE_NUM_TEXELS - 1)) &&
            (group_thread_id.y == 0 || group_thread_id.y == (DDGI_PROBE_NUM_TEXELS - 1));
    bool is_row_texel = (group_thread_id.x > 0 && group_thread_id.x < (DDGI_PROBE_NUM_TEXELS - 1));

    uint max_probes_per_row = DDGI_MAX_PROBE_COUNTS.x * DDGI_MAX_PROBE_COUNTS.y;

    uint actual_per_row = volume.probe_counts.x * volume.probe_counts.y;
    uint grid_z = probe_idx / actual_per_row;
    uint grid_y = (probe_idx % actual_per_row) / volume.probe_counts.x;
    uint grid_x = probe_idx % volume.probe_counts.x;

    uint atlas_idx = grid_z * max_probes_per_row + grid_y * DDGI_MAX_PROBE_COUNTS.x + grid_x;

    uint probe_atlas_x = atlas_idx % max_probes_per_row;
    uint probe_atlas_y = atlas_idx / max_probes_per_row;

    ivec2 probe_base = ivec2(
            probe_atlas_x * DDGI_PROBE_NUM_TEXELS,
            probe_atlas_y * DDGI_PROBE_NUM_TEXELS
        );

    ivec2 copy_offset = ivec2(0, 0);

    if (is_corner_texel) {
        copy_offset.x = int(group_thread_id.x) > 0 ? 1 : DDGI_PROBE_NUM_INTERIOR_TEXELS;
        copy_offset.y = int(group_thread_id.y) > 0 ? 1 : DDGI_PROBE_NUM_INTERIOR_TEXELS;
    } else if (is_row_texel) {
        copy_offset.x = (DDGI_PROBE_NUM_TEXELS - 1) - int(group_thread_id.x);
        copy_offset.y = int(group_thread_id.y) + ((int(group_thread_id.y) > 0) ? -1 : 1);
    } else {
        copy_offset.x = int(group_thread_id.x) + ((int(group_thread_id.x) > 0) ? -1 : 1);
        copy_offset.y = (DDGI_PROBE_NUM_TEXELS - 1) - int(group_thread_id.y);
    }

    ivec2 copy_coords = probe_base + copy_offset;
    ivec2 dest_coords = probe_base + ivec2(group_thread_id.xy);

    #if defined(DDGI_BLEND_IRRADIANCE)
    imageStore(out_irradiance, dest_coords, imageLoad(out_irradiance, copy_coords));
    #endif

    #if defined(DDGI_BLEND_DEPTH)
    imageStore(out_depth, dest_coords, imageLoad(out_depth, copy_coords));
    #endif
}

void main() {
    uvec3 dispatch_thread_id = gl_GlobalInvocationID;
    uvec3 group_thread_id = gl_LocalInvocationID;
    uvec3 group_id = gl_WorkGroupID;
    uint group_index = gl_LocalInvocationIndex;

    bool is_border_texel = (group_thread_id.x == 0 || group_thread_id.x == (DDGI_PROBE_NUM_INTERIOR_TEXELS + 1));
    is_border_texel = is_border_texel || (group_thread_id.y == 0 || group_thread_id.y == (DDGI_PROBE_NUM_INTERIOR_TEXELS + 1));

    uint probes_per_row = volume.probe_counts.x * volume.probe_counts.y;
    uint probe_idx = group_id.z * probes_per_row + group_id.y * volume.probe_counts.x + group_id.x;

    uint probe_count = volume.probe_counts.x * volume.probe_counts.y * volume.probe_counts.z;

    if (probe_idx >= probe_count) {
        return;
    }

    int physical_probe_idx = ddgi_physical_probe_index(int(probe_idx), volume);
    if (probes[physical_probe_idx].state == 0) {
        return;
    }

    load_shared_memory(int(probe_idx), group_index);

    uint max_probes_per_row = DDGI_MAX_PROBE_COUNTS.x * DDGI_MAX_PROBE_COUNTS.y;
    uint actual_per_row = volume.probe_counts.x * volume.probe_counts.y;

    uint grid_z = physical_probe_idx / actual_per_row;
    uint grid_y = (physical_probe_idx % actual_per_row) / volume.probe_counts.x;
    uint grid_x = physical_probe_idx % volume.probe_counts.x;

    uint atlas_idx = grid_z * max_probes_per_row
            + grid_y * DDGI_MAX_PROBE_COUNTS.x
            + grid_x;

    uint probe_atlas_x = atlas_idx % max_probes_per_row;
    uint probe_atlas_y = atlas_idx / max_probes_per_row;

    ivec2 probe_base_coord = ivec2(
            probe_atlas_x * DDGI_PROBE_NUM_TEXELS,
            probe_atlas_y * DDGI_PROBE_NUM_TEXELS
        );

    if (!is_border_texel) {
        ivec2 atlas_coord = probe_base_coord + ivec2(group_thread_id.xy);

        ivec2 probe_texel = ivec2(group_thread_id.xy) - ivec2(1, 1);
        vec2 oct_uv = (vec2(probe_texel) + 0.5) / float(DDGI_PROBE_NUM_INTERIOR_TEXELS);
        vec3 probe_direction = oct_decode(oct_uv);

        #if defined(DDGI_BLEND_IRRADIANCE)
        uint backface_count = 0;
        uint max_backfaces = uint(float(volume.rays_per_probe - DDGI_NUM_FIXED_RAYS) * volume.random_ray_backface_threshold);
        #endif

        vec4 result = vec4(0.0);
        for (int r = DDGI_NUM_FIXED_RAYS; r < volume.rays_per_probe; r++) {
            vec3 ray_direction = shared_ray_direction[r];

            float weight = max(0.0, dot(ray_direction, probe_direction));

            #if defined(DDGI_BLEND_IRRADIANCE)
            vec3 ray_irradiance = shared_ray_irradiance[r];
            float ray_distance = shared_ray_distance[r];

            if (ray_distance < 0.0) {
                backface_count++;

                if (backface_count >= max_backfaces) {
                    return;
                }

                continue;
            }

            result += vec4(ray_irradiance * weight, weight);
            #endif

            #if defined(DDGI_BLEND_DEPTH)
            float ray_max_distance = length(volume.probe_spacing) * 1.5;

            weight = pow(weight, volume.distance_exponent);
            float ray_distance = min(abs(shared_ray_distance[r]), ray_max_distance);

            result += vec4(ray_distance * weight, (ray_distance * ray_distance) * weight, 0.0, weight);
            #endif
        }

        float epsilon = float(volume.rays_per_probe - DDGI_NUM_FIXED_RAYS);
        epsilon *= 1e-9f;

        result.rgb *= 1.0 / (2.0 * max(result.a, epsilon));
        result.a = 1.0;

        #if defined(DDGI_BLEND_IRRADIANCE)
        result.rgb = pow(result.rgb, vec3(1.0 / volume.irradiance_encoding_gamma));
        vec3 history = imageLoad(irradiance_history, atlas_coord).rgb;
        #endif

        #if defined(DDGI_BLEND_DEPTH)
        vec3 history = imageLoad(depth_history, atlas_coord).rgb;
        #endif

        float hysteresis = volume.hysteresis;
        if (dot(history, history) == 0) hysteresis = 0.0;

        #if defined(DDGI_BLEND_IRRADIANCE)
        if (component_max(history - result.rgb) > volume.irradiance_threshold) {
            hysteresis = max(0.0, hysteresis - 0.75);
        }

        vec3 delta = (result.rgb - history);
        if (linear_rgb_to_luminance(delta) > volume.brightness_threshold) {
            delta *= 0.25;
        }

        const float c_threshold = 1.0 / 1024.0;
        vec3 lerp_delta = (1.0 - hysteresis) * delta;
        if (component_max(result.rgb) < component_max(history.rgb)) {
            lerp_delta = min(max(vec3(c_threshold), abs(lerp_delta)), abs(delta)) * sign(lerp_delta);
        }
        result = vec4(history + lerp_delta, 1.0);
        imageStore(out_irradiance, atlas_coord, result);
        #endif

        #if defined(DDGI_BLEND_DEPTH)
        result = vec4(mix(result.rg, history.rg, hysteresis), 0.0, 1.0);
        imageStore(out_depth, atlas_coord, result);
        #endif

        return;
    }

    memoryBarrier();
    barrier();

    update_border_texel(group_thread_id, group_thread_id, physical_probe_idx, probes_per_row);
}
#endif
