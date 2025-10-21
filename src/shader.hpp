#pragma once

#include "ember.hpp"

#include <spirv_reflect.h>

enum class DescriptorType {
    IMAGE,
    BUFFER,
    ACCELERATION_STRUCTURE
};

struct DescriptorInfo {
    union {
        VkDescriptorImageInfo      image_info;
        VkDescriptorBufferInfo     buffer_info;
        VkAccelerationStructureKHR acceleration_structure_info;
    };
    DescriptorType type;

    DescriptorInfo() {
    }

    DescriptorInfo(VkAccelerationStructureKHR structure) {
        acceleration_structure_info = structure;
        type                        = DescriptorType::ACCELERATION_STRUCTURE;
    }

    DescriptorInfo(VkImageView image_view, VkImageLayout image_layout = VK_IMAGE_LAYOUT_GENERAL) {
        image_info.sampler     = VK_NULL_HANDLE;
        image_info.imageView   = image_view;
        image_info.imageLayout = image_layout;
        type                   = DescriptorType::IMAGE;
    }

    DescriptorInfo(VkSampler sampler, VkImageView image_view, VkImageLayout image_layout = VK_IMAGE_LAYOUT_GENERAL) {
        image_info.sampler     = sampler;
        image_info.imageView   = image_view;
        image_info.imageLayout = image_layout;
        type                   = DescriptorType::IMAGE;
    }

    DescriptorInfo(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
        buffer_info.buffer = buffer;
        buffer_info.offset = offset;
        buffer_info.range  = range;
        type               = DescriptorType::BUFFER;
    }

    DescriptorInfo(VkBuffer buffer) {
        buffer_info.buffer = buffer;
        buffer_info.offset = 0;
        buffer_info.range  = VK_WHOLE_SIZE;
        type               = DescriptorType::BUFFER;
    }
};

struct DescriptorBinding {
    uint32_t         set;
    uint32_t         binding;
    VkDescriptorType type;
};

// TODO: rename
struct Shader2 {
    VkShaderStageFlagBits stage;

    std::vector<DescriptorBinding> layout_bindings;

    VkShaderModule module = VK_NULL_HANDLE;

    uint32_t workgroup_size_x;
    uint32_t workgroup_size_y;
    uint32_t workgroup_size_z;
};

struct Program {
    std::vector<Shader2> shaders;

    VkDescriptorSetLayout      descriptor_set_layout      = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplate descriptor_update_template = VK_NULL_HANDLE;

    VkPipelineBindPoint bind_point;
    VkPipelineLayout    pipeline_layout = VK_NULL_HANDLE;

    uint32_t           push_constants_size;
    VkShaderStageFlags stages;

    uint32_t workgroup_size_x;
    uint32_t workgroup_size_y;
    uint32_t workgroup_size_z;

    std::vector<VkDescriptorType> descriptor_types;
    VkDescriptorSet               allocated_set     = VK_NULL_HANDLE;
    VkDescriptorSetLayout         additional_layout = VK_NULL_HANDLE;
};

Shader2 load_shader(VkDevice device, const std::filesystem::path& path);

void destroy_shader(VkDevice device, const Shader2& shader);

// NOTE: if allocation_pool is provided, a descriptor set will be allocated
// and used by the program automatically, instead of push descriptors
Program create_shader_program(
    VkDevice                    device,
    const std::vector<Shader2>& shaders,
    VkPipelineBindPoint         bind_point,
    uint32_t                    push_constants_size = 0,
    VkDescriptorSetLayout       additional_layout   = VK_NULL_HANDLE,
    VkDescriptorPool            allocation_pool     = VK_NULL_HANDLE
);

void destroy_shader_program(VkDevice device, const Program& program);

VkPipeline sp_create_compute_pipeline(VkDevice device, const Program& program);
VkPipeline
sp_create_graphics_pipeline(VkDevice device, const Program& program, VkPipelineRenderingCreateInfo rendering_info);
VkPipeline create_ray_tracing_pipeline(VkDevice device, const Program& program);

void dispatch(
    VkDevice                           device,
    VkCommandBuffer                    command_buffer,
    const Program&                     program,
    uint32_t                           thread_count_x,
    uint32_t                           thread_count_y,
    uint32_t                           thread_count_z,
    const std::vector<DescriptorInfo>& descriptors,
    const void*                        push_constants,
    VkDescriptorSet                    additional_set = VK_NULL_HANDLE
);

void trace_rays(
    VkDevice                               device,
    VkCommandBuffer                        command_buffer,
    const Program&                         program,
    const VkStridedDeviceAddressRegionKHR* raygen_sbt,
    const VkStridedDeviceAddressRegionKHR* miss_sbt,
    const VkStridedDeviceAddressRegionKHR* hit_sbt,
    const VkStridedDeviceAddressRegionKHR* callable_sbt,
    uint32_t                               width,
    uint32_t                               height,
    uint32_t                               depth,
    const std::vector<DescriptorInfo>&     descriptors,
    const void*                            push_constants,
    VkDescriptorSet                        additional_set = VK_NULL_HANDLE
);

void bind_program(
    VkDevice                           device,
    VkCommandBuffer                    command_buffer,
    const Program&                     program,
    const std::vector<DescriptorInfo>& descriptors,
    const void*                        push_constants,
    VkDescriptorSet                    additional_set = VK_NULL_HANDLE
);
