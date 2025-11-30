struct DDGIRay {
    vec3 direction;
    float distance;

    vec3 radiance;
    float distance_squared;
};

ivec2 probe_index_to_atlas_coord(ivec3 probe_coords, ivec3 probe_counts, int probes_per_row) {
    int linear_index = probe_coords.x +
            probe_coords.z * probe_counts.x +
            probe_coords.y * probe_counts.x * probe_counts.z;

    return ivec2(linear_index % probes_per_row, linear_index / probes_per_row);
}

vec3 get_probe_world_position(ivec3 probe_coords, vec3 grid_origin, float probe_spacing) {
    return grid_origin + vec3(probe_coords) * probe_spacing;
}
