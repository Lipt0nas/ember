#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"
#include "ddgi_common.glsl"

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in flat int in_probe_index;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D ddgi_irradiance;
layout(set = 0, binding = 1) uniform sampler2D ddgi_depth;

layout(set = 0, binding = 3, std430) readonly buffer ProbeBuffer {
    DDGIProbe probes[];
};

layout(set = 0, binding = 4, std430) readonly uniform LightingData {
    LightingUBO lighting;
};

layout(set = 0, binding = 5, std430) readonly buffer DDGIVolumeData {
    DDGIVolume volume;
};

layout(push_constant, std430) uniform pc {
    mat4 combined_matrix;
    vec3 camera_pos;
} push;

#include "ddgi_sample.glsl"

void main() {
    int probe_count = volume.probe_counts.x * volume.probe_counts.y * volume.probe_counts.z;

    if (in_probe_index < probe_count) {
        DDGIProbe probe = probes[in_probe_index];

        vec3 irradiance = vec3(0.0);
        bool valid_probe = probe.state == 1;

        if (valid_probe) {
            irradiance = texture(ddgi_irradiance, ddgi_probe_uv(volume, in_probe_index, normalize(in_normal), DDGI_PROBE_NUM_RADIANCE_INTERIOR_TEXELS)).rgb;

            vec3 exponent = vec3(volume.irradiance_encoding_gamma * 0.5);
            irradiance = pow(irradiance, exponent);
            irradiance *= irradiance;
            irradiance *= PI2;
            irradiance *= 0.0002;
            irradiance *= 1.0 / volume.intensity;

            irradiance.x = GranTurismoTonemapper(irradiance.x);
            irradiance.y = GranTurismoTonemapper(irradiance.y);
            irradiance.z = GranTurismoTonemapper(irradiance.z);

            irradiance = pow(irradiance, vec3(1.0 / 2.2));
        } else {
            irradiance = vec3(1.0, 0.0, 0.0);
        }

        out_color = vec4(irradiance, 1.0);
    } else {
        out_color = vec4(1.0, 0.0, 0.0, 0.0);
    }
}
