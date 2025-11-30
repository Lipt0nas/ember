#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = in_color;
}
