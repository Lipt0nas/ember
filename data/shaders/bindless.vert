#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

layout(location = 0) out vec3 out_color;
layout(location = 1) out vec2 out_uv;
layout(location = 2) flat out uint out_albedo_index;

struct DrawData {
    mat4 model;

    uint first_index;
    int vertex_offset;

    uint meshlet_offset;
    uint meshlet_count;

    uint albedo_index;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
};

struct Meshlet {
    uint vertex_offset;
    uint triangle_offset;

    uint vertex_count;
    uint triangle_count;
};

layout(buffer_reference, scalar) readonly buffer IndexBuffer {
    uint indices[];
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    float data[];
};

layout(buffer_reference, scalar) readonly buffer MeshletBuffer {
    Meshlet meshlets[];
};

layout(buffer_reference, scalar) readonly buffer MeshletVertexIndices {
    uint indices[];
};

layout(buffer_reference, scalar) readonly buffer MeshletPrimitiveIndices {
    uint8_t indices[];
};

layout(set = 0, binding = 0) readonly buffer DrawDataBuffer {
    DrawData draw_data[];
} uniforms;

layout(push_constant, std430) uniform pc {
    mat4 transform;
    VertexBuffer vertex_buffer;
    IndexBuffer index_buffer;
    MeshletBuffer meshlet_buffer;
    MeshletVertexIndices meshlet_vertex_indices;
    MeshletPrimitiveIndices meshlet_primitive_indices;
} push_constants;

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

    out_color = normal;
    out_uv = uv;
    out_albedo_index = draw.albedo_index;

    gl_Position = push_constants.transform * draw.model * vec4(position, 1.0);
}
