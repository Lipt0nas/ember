#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "common.glsl"

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_world_pos;
layout(location = 3) flat in int in_material_index;

layout(scalar, set = 1, binding = 3) readonly buffer Materials {
    Material materials[];
};

layout(set = 4, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_emission;

vec3 world_normal(vec3 normal_map, vec3 vertex_normal, vec3 world_pos, vec2 uv) {
    vec3 dp1 = dFdx(world_pos);
    vec3 dp2 = dFdy(world_pos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 N = normalize(vertex_normal);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);

    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    mat3 TBN = mat3(T * invmax, B * invmax, N);

    return normalize(TBN * normal_map);
}

void main() {
    Material material = materials[in_material_index];

    vec4 albedo = texture(textures[nonuniformEXT(material.albedo_index)], in_uv).rgba;
    if (albedo.a < 0.2) {
        discard;
    }

    vec3 normal = texture(textures[nonuniformEXT(material.normals_index)], in_uv).rgb * 2.0 - 1.0;
    vec3 metallic_roughness = texture(textures[nonuniformEXT(material.material_index)], in_uv).rgb;

    out_color = vec4(albedo.rgb, metallic_roughness.y * material.roughness_multiplier);
    out_normal = vec4(world_normal(normal, in_normal, in_world_pos, in_uv), MAP_METALLIC(metallic_roughness.z) * material.metallic_multiplier);
    out_emission = vec4(material.emissive_color, 1.0);
}
