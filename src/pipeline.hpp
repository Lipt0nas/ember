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
    VkShaderModule        module;
    VkShaderStageFlagBits stage;
};

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

VkDescriptorSetLayout
create_descriptor_set_layout(VkDevice device, const std::vector<DescriptorLayoutBinding>& bindings);
