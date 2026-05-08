#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "common.glsl"

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_tangent_sign;
layout(location = 3) in vec3 in_world_pos;
layout(location = 4) in vec4 in_clip_pos;
layout(location = 5) in vec4 in_last_clip_pos;
layout(location = 6) flat in int in_material_index;
layout(location = 7) flat in int in_draw_id;

layout(scalar, set = 1, binding = 3) readonly buffer Materials {
    Material materials[];
};

layout(set = 4, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec3 out_emission;
layout(location = 3) out uint out_id;

float compute_roughness_adjustment(vec3 normal, float roughness) {
    vec3 dndx = dFdx(normal);
    vec3 dndy = dFdy(normal);

    float variance = 0.5 * (dot(dndx, dndx) + dot(dndy, dndy));

    float r2 = roughness * roughness;

    return sqrt(clamp(r2 + variance, 0.0, 1.0));
}

void main() {
    Material material = materials[in_material_index];

    vec4 albedo = material_get_albedo(material, textures[nonuniformEXT(material.albedo_index)], in_uv);
    vec3 emissive = material_get_emissive(material, textures[nonuniformEXT(material.emissive_index)], in_uv);
    vec3 normal = material_get_normal(material, textures[nonuniformEXT(material.normals_index)], in_uv);
    vec2 rougness_metallic = material_get_roughness_metallic(material, textures[nonuniformEXT(material.material_index)], in_uv);

    vec2 screen = (in_clip_pos.xy / in_clip_pos.w) * 0.5 + 0.5;
    vec2 last_screen = (in_last_clip_pos.xy / in_last_clip_pos.w) * 0.5 + 0.5;

    vec3 N = normalize(in_normal);
    vec3 T = normalize(in_tangent_sign.xyz);
    vec3 B = cross(N, T) * in_tangent_sign.w;

    vec3 w_normal = normalize(normal.r * in_tangent_sign.xyz + normal.g * B + normal.b * in_normal);

    out_color = vec4(albedo.rgb, compute_roughness_adjustment(w_normal, rougness_metallic.x));
    out_normal = vec4(pack_normals(w_normal), rougness_metallic.y);
    out_emission = emissive;
    out_id = in_draw_id;

    if (albedo.a < 0.2) {
        discard;
    }
}
