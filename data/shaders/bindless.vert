#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec2 out_uv;
layout(location = 2) out vec3 out_meshlet_color;
layout(location = 3) flat out uint out_albedo_index;
layout(location = 4) flat out uint out_normals_index;
layout(location = 5) flat out uint out_material_index;
layout(location = 6) out vec3 out_world_pos;

#include "common.glsl"
#include "common_geometry.glsl"

void main() {
    DrawData draw = uniforms.draw_data[gl_DrawID];

    uint base = gl_VertexIndex * 8;
    vec3 position = vec3(
            push_constants.vertex_buffer.data[base + 0],
            push_constants.vertex_buffer.data[base + 1],
            push_constants.vertex_buffer.data[base + 2]
        );
    vec3 normal = vec3(
            push_constants.vertex_buffer.data[base + 3],
            push_constants.vertex_buffer.data[base + 4],
            push_constants.vertex_buffer.data[base + 5]
        );
    vec2 uv = vec2(
            push_constants.vertex_buffer.data[base + 6],
            push_constants.vertex_buffer.data[base + 7]
        );

    vec4 world_pos = scene.view_proj * draw.model * vec4(position, 1.0);
    vec3 meshlet_color = vec3(1.0);

    gl_Position = world_pos;
    out_normal = normal;
    out_uv = uv;
    out_meshlet_color = meshlet_color;
    out_albedo_index = draw.albedo_index;
    out_normals_index = draw.normals_index;
    out_material_index = draw.material_index;
    out_world_pos = world_pos.xyz;
}
