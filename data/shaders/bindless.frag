#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_meshlet_color;
layout(location = 3) flat in uint in_albedo_index;
layout(location = 4) flat in uint in_normals_index;
layout(location = 5) flat in uint in_material_index;
layout(location = 6) in vec3 in_world_pos;

layout(set = 4, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_material;

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
    vec3 albedo = texture(textures[nonuniformEXT(in_albedo_index)], in_uv).rgb;
    vec3 normal = texture(textures[nonuniformEXT(in_normals_index)], in_uv).rgb * 2.0 - 1.0;
    vec3 material = texture(textures[nonuniformEXT(in_material_index)], in_uv).rgb;

    out_color = vec4(albedo, 1.0);
    out_normal = vec4(world_normal(normal, in_normal, in_world_pos, in_uv), 1.0);
    out_material = vec4(material, 1.0);
}
