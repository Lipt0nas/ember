#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"
#include "ddgi_common.glsl"

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D ddgi_irradiance;
layout(set = 0, binding = 1) uniform sampler2D ddgi_depth;

layout(set = 0, binding = 3, std430) readonly uniform LightingData {
    LightingUBO lighting;
};

layout(push_constant, std430) uniform pc {
    mat4 combined_matrix;
    vec3 camera_pos;
} push;

void main() {
    vec3 V = normalize(push.camera_pos - in_pos);
    vec3 irradiance = sample_ddgi(lighting, in_pos, in_normal, V, ddgi_irradiance, ddgi_depth);

    irradiance.x = GranTurismoTonemapper(irradiance.x);
    irradiance.y = GranTurismoTonemapper(irradiance.y);
    irradiance.z = GranTurismoTonemapper(irradiance.z);

    out_color = vec4(irradiance, 1.0);
}
