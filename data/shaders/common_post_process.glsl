layout(push_constant, std430) uniform pc {
    mat4 transform;
    mat4 inv_transform;

    vec4 camera_position;

    uint depth_index;
    uint albedo_index;
    uint normals_index;
    uint material_index;

    uint lightpass_index;
} push_constants;
