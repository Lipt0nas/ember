#include "common.glsl"

layout(push_constant, std430) uniform pc {
    uint depth_index;
    uint albedo_index;
    uint normals_index;
    uint material_index;

    uint lightpass_index;
} push_constants;
