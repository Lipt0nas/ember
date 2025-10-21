#version 460

#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_scalar_block_layout : require

#include "common.glsl"

struct HitPayload {
    vec3 world_pos;
    vec3 normal;
    vec3 radiance;
    bool hit;
};

layout(location = 0) rayPayloadInEXT HitPayload hit;

void main() {
    hit.hit = false;
}
