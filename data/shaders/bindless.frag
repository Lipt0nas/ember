#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) flat in uint in_albedo_index;
layout(location = 3) in vec3 in_meshlet_color;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 1) uniform sampler2D textures[];

vec3 apply_light(vec3 normal, vec3 light_dir, vec3 light_color) {
    return max(dot(normalize(light_dir), normal), 0.0) * light_color;
}

void main() {
    vec3 albedo = texture(textures[in_albedo_index], in_uv).rgb;

    vec3 diffuse = apply_light(in_normal, vec3(0.5, -0.5, 0.5), vec3(1, 0.8, 0.8));
    diffuse += apply_light(in_normal, vec3(-0.5, 0.5, 0.5), vec3(1, 0.5, 0.5));

    out_color = vec4((diffuse * (albedo)) + albedo * 0.1, 1.0);
}
