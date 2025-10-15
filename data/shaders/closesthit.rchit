#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing : enable

#include "common_geometry.glsl"

struct HitPayload {
    vec3 world_pos;
    vec3 normal;
    vec3 radiance;
    bool hit;
};

layout(binding = 0, set = 3) uniform accelerationStructureEXT top_level_as;
layout(location = 0) rayPayloadInEXT HitPayload payload;

layout(set = 2, binding = 0) uniform sampler2D textures[];

hitAttributeEXT vec3 attribs;

bool trace_shadow_ray(vec3 origin, vec3 normal, vec3 direction) {
    vec3 stored_pos = payload.world_pos;
    vec3 stored_normal = payload.normal;
    vec3 stored_radiance = payload.radiance;
    bool stored_hit = payload.hit;

    payload.hit = false;
    traceRayEXT(top_level_as,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsSkipClosestHitShaderEXT,
        0xFF, 0, 0, 0, origin + normal * 0.001, 0.001, direction, 10000.0, 0);

    bool hit = payload.hit;

    payload.world_pos = stored_pos;
    payload.normal = stored_normal;
    payload.radiance = stored_radiance;
    payload.hit = stored_hit;

    return hit;
}

vec3 evaluate_lighting(vec3 pos, vec3 normal) {
    vec3 lighting = vec3(0.0);

    vec3 sun_dir = normalize(-vec3(0.4, -0.7, 0.5));
    vec3 sun_color = vec3(1.0, 1.0, 1.0) * 5.0;

    float NdotL = max(0.0, dot(normal, sun_dir));
    if (NdotL > 0.0) {
        bool in_shadow = trace_shadow_ray(pos, normal, sun_dir);

        if (!in_shadow) {
            lighting += sun_color * NdotL;
        }
    }

    return lighting;
}

void main() {
    const vec3 bary = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

    uint object_id = gl_InstanceCustomIndexEXT;
    DrawData draw = uniforms.draw_data[object_id];

    uint index_offset = draw.first_index + gl_PrimitiveID * 3;
    uint i0 = global_indices.indices[index_offset + 0];
    uint i1 = global_indices.indices[index_offset + 1];
    uint i2 = global_indices.indices[index_offset + 2];

    uint vertex_offset = draw.vertex_offset;
    Vertex v0 = global_vertices.vertices[vertex_offset + i0];
    Vertex v1 = global_vertices.vertices[vertex_offset + i1];
    Vertex v2 = global_vertices.vertices[vertex_offset + i2];

    vec3 pos0 = v0.position;
    vec3 pos1 = v1.position;
    vec3 pos2 = v2.position;
    vec3 local_pos = pos0 * bary.x + pos1 * bary.y + pos2 * bary.z;

    vec3 n0 = v0.normal;
    vec3 n1 = v1.normal;
    vec3 n2 = v2.normal;
    vec3 local_normal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);

    payload.world_pos = vec3(gl_ObjectToWorldEXT * vec4(local_pos, 1.0));
    payload.normal = normalize(mat3(gl_ObjectToWorldEXT) * local_normal);
    payload.hit = true;

    vec2 uv0 = v0.uv;
    vec2 uv1 = v1.uv;
    vec2 uv2 = v2.uv;
    vec2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    vec3 albedo = vec3(1.0);
    if (draw.albedo_index != -1) {
        albedo = texture(textures[nonuniformEXT(draw.albedo_index)], uv).rgb;
    }

    vec3 lighting = evaluate_lighting(payload.world_pos, payload.normal);
    payload.radiance = albedo * lighting;
}
