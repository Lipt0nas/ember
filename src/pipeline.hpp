#pragma once

#include "ember.hpp"

struct DescriptorLayoutBinding {
    uint32_t           binding;
    VkDescriptorType   type;
    uint32_t           count;
    VkShaderStageFlags stage_flags;
    bool               bindless;
};

struct Shader {
    VkShaderModule        module = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage;
};

Shader shader_from_file(VkDevice device, VkShaderStageFlagBits stage, const std::filesystem::path& path);

VkShaderModule shader_module_from_file(VkDevice device, const std::filesystem::path& path);

VkPipelineLayout create_pipeline_layout(
    VkDevice                                  device,
    const std::vector<VkDescriptorSetLayout>& descriptor_set_layouts,
    VkShaderStageFlags                        push_constants_stages = 0,
    uint32_t                                  push_constants_size   = 0
);

VkPipeline create_graphics_pipeline(
    VkDevice                     device,
    VkPipelineLayout             pipeline_layout,
    const std::vector<Shader>&   shaders,
    const std::vector<VkFormat>& color_attachment_formats,
    VkFormat                     depth_format = VK_FORMAT_UNDEFINED
);

VkPipeline create_compute_pipeline(VkDevice device, VkPipelineLayout pipeline_layout, const Shader& shader);

VkDescriptorSetLayout create_descriptor_set_layout(
    VkDevice device, const std::vector<DescriptorLayoutBinding>& bindings, bool push_set = false
);

// ------------------

enum class DescriptorType {
    NONE,
    IMAGE,
    BUFFER,
    ACCELERATION_STRUCTURE
};

struct DescriptorInfo {
    union {
        VkDescriptorImageInfo      image_info;
        VkDescriptorBufferInfo     buffer_info;
        VkAccelerationStructureKHR acceleration_structure;
    };
    DescriptorType type = DescriptorType::NONE;

    DescriptorInfo() {
    }

    DescriptorInfo(VkAccelerationStructureKHR structure) {
        acceleration_structure = structure;
        type                   = DescriptorType::ACCELERATION_STRUCTURE;
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
    VkDescriptorType type;
    bool             is_array;

    DescriptorInfo write_info = {};
};

struct DescriptorLayout {
    std::vector<DescriptorBinding> bindings;

    bool is_push_set = false;
};

struct Pipeline {
    std::vector<Shader> shaders;
    VkShaderStageFlags  stage_flags;

    std::vector<DescriptorLayout>      set_layouts;
    std::vector<VkDescriptorSetLayout> layout_handles;

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline       pipeline_handle = VK_NULL_HANDLE;

    VkPipelineBindPoint bind_point;
};

std::vector<VkDescriptorSet>
allocate_descriptor_sets(VkDevice device, VkDescriptorPool descriptor_pool, const Pipeline& pipeline);

Pipeline create_graphics_pipeline2(
    VkDevice                             device,
    const std::vector<Shader>&           shaders,
    const std::vector<DescriptorLayout>& descriptor_sets,
    const std::vector<VkFormat>&         color_attachment_formats,
    VkFormat                             depth_format        = VK_FORMAT_UNDEFINED,
    uint32_t                             push_constants_size = 0,
    VkDescriptorSetLayout                additional_set      = VK_NULL_HANDLE
);

Pipeline create_compute_pipeline2(
    VkDevice                             device,
    const Shader&                        shader,
    const std::vector<DescriptorLayout>& descriptor_sets,
    uint32_t                             push_constants_size = 0,
    VkDescriptorSetLayout                additional_set      = VK_NULL_HANDLE
);

void destroy_pipeline(VkDevice device, const Pipeline& pipeline);

// ------------------
