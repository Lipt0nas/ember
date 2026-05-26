#version 460
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "common.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) flat in int in_data_index;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec3 out_emission;
layout(location = 3) out uint out_id;

layout(scalar, set = 0, binding = 3) readonly buffer Materials {
    Material materials[];
};

layout(set = 1, binding = 0) uniform sampler2D textures[];

void main() {
    vec2 uv = vec2(in_uv.x, in_uv.y);

    vec4 albedo = texture(textures[nonuniformEXT(in_data_index)], in_uv) * in_color;
    vec3 emissive = vec3(0.0);
    vec2 rougness_metallic = vec2(1.0, 0.0);

    vec3 vertex_normal = gl_FrontFacing ? in_normal : -in_normal;

    out_color = vec4(albedo.rgb, rougness_metallic.x);
    out_normal = vec4(pack_normals(vertex_normal), rougness_metallic.y);
    out_emission = emissive;
    out_id = 25565;

    if (albedo.a < 0.2) {
        discard;
    }
}
