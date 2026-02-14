#include "pipeline.hpp"

VkDescriptorSetLayout create_descriptor_set_layout(
    VkDevice device, VkShaderStageFlags stage_flags, const std::vector<DescriptorBinding>& bindings, bool push_set
) {
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    std::vector<VkDescriptorBindingFlags>     flags;

    // TODO: does this even make sense?
    bool contains_bindless = false;

    uint32_t binding_idx = 0;
    for (auto& binding : bindings) {
        vk_bindings.push_back(
            VkDescriptorSetLayoutBinding{
                .binding            = binding_idx++,
                .descriptorType     = binding.type,
                .descriptorCount    = static_cast<uint32_t>(binding.is_array ? 10000 : 1),
                .stageFlags         = stage_flags,
                .pImmutableSamplers = nullptr
            }
        );

        flags.push_back(
            binding.is_array
                ? VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                : 0
        );

        if (binding.is_array) {
            contains_bindless = true;
        }
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount  = static_cast<uint32_t>(flags.size()),
        .pBindingFlags = flags.data(),
    };

    VkDescriptorSetLayoutCreateFlags layout_flags = 0;

    if (contains_bindless) {
        layout_flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }

    if (push_set) {
        layout_flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    }

    VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = &binding_flags_info,
        .flags        = layout_flags,
        .bindingCount = static_cast<uint32_t>(vk_bindings.size()),
        .pBindings    = vk_bindings.data()
    };

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_layout_info, nullptr, &layout));

    return layout;
}

Pipeline create_graphics_pipeline(
    VkDevice                             device,
    const std::vector<Shader>&           shaders,
    const std::vector<DescriptorLayout>& descriptor_sets,
    const std::vector<VkFormat>&         color_attachment_formats,
    VkFormat                             depth_format,
    uint32_t                             push_constants_size,
    VkDescriptorSetLayout                additional_set
) {
    VkShaderStageFlags shader_stage_flags = 0;
    for (auto& shader : shaders) {
        shader_stage_flags |= (VkShaderStageFlags)shader.stage;
    }

    std::vector<VkDescriptorSetLayout> set_layouts;
    for (auto& set : descriptor_sets) {
        set_layouts.push_back(create_descriptor_set_layout(device, shader_stage_flags, set.bindings, set.is_push_set));
    }

    VkPushConstantRange push_constants_range = {
        .stageFlags = shader_stage_flags,
        .offset     = 0,
        .size       = push_constants_size,
    };

    bool push_constants_present = push_constants_size > 0;

    if (additional_set != VK_NULL_HANDLE) {
        set_layouts.push_back(additional_set);
    }

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts            = set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(push_constants_present ? 1 : 0),
        .pPushConstantRanges    = push_constants_present ? &push_constants_range : nullptr
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout));

    // TODO: this is ugly as sin
    if (additional_set != VK_NULL_HANDLE) {
        set_layouts.pop_back();
    }

    VkPipelineRenderingCreateInfo pipeline_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = static_cast<uint32_t>(color_attachment_formats.size()),
        .pColorAttachmentFormats = color_attachment_formats.data(),
        .depthAttachmentFormat   = depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    // Vertex pulling anyway, can leave this blank
    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // Left to dynamic states
    VkViewport viewport = {.x = 0, .y = 0, .width = 0, .height = 0, .minDepth = 0.0f, .maxDepth = 1.0f};
    VkRect2D   scissor  = {.offset = {.x = 0, .y = 0}, .extent = {.width = 0, .height = 0}};

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .viewportCount = 1,
        .pViewports    = &viewport,
        .scissorCount  = 1,
        .pScissors     = &scissor
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 0.0f,
        .pSampleMask           = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f
    };

    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachment_states;
    for (VkFormat format : color_attachment_formats) {
        color_blend_attachment_states.push_back({
            .blendEnable         = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        });
    }

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32_t>(color_blend_attachment_states.size()),
        .pAttachments    = color_blend_attachment_states.data(),
        .blendConstants  = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_GREATER,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .front                 = {},
        .back                  = {},
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f
    };

    std::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos;
    for (auto& shader : shaders) {
        shader_stage_infos.push_back(
            VkPipelineShaderStageCreateInfo{
                .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext               = nullptr,
                .flags               = 0,
                .stage               = shader.stage,
                .module              = shader.module,
                .pName               = "main",
                .pSpecializationInfo = nullptr
            }
        );
    }

    std::vector<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext             = nullptr,
        .flags             = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates    = dynamic_states.data()
    };

    VkGraphicsPipelineCreateInfo graphics_pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &pipeline_rendering_info,
        .flags               = 0,
        .stageCount          = static_cast<uint32_t>(shader_stage_infos.size()),
        .pStages             = &shader_stage_infos[0],
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pTessellationState  = nullptr,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .pDynamicState       = &dynamic_state_info,
        .layout              = pipeline_layout,
        .renderPass          = VK_NULL_HANDLE,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, &pipeline));

    return Pipeline{
        .shaders         = shaders,
        .stage_flags     = shader_stage_flags,
        .set_layouts     = descriptor_sets,
        .layout_handles  = set_layouts,
        .pipeline_layout = pipeline_layout,
        .pipeline_handle = pipeline,
        .bind_point      = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };
}

Pipeline create_debug_render_pipeline(
    VkDevice                             device,
    const std::vector<Shader>&           shaders,
    const std::vector<DescriptorLayout>& descriptor_sets,
    const std::vector<VkFormat>&         color_attachment_formats,
    VkFormat                             depth_format,
    uint32_t                             push_constants_size
) {
    VkShaderStageFlags shader_stage_flags = 0;
    for (auto& shader : shaders) {
        shader_stage_flags |= (VkShaderStageFlags)shader.stage;
    }

    std::vector<VkDescriptorSetLayout> set_layouts;
    for (auto& set : descriptor_sets) {
        set_layouts.push_back(create_descriptor_set_layout(device, shader_stage_flags, set.bindings, set.is_push_set));
    }

    VkPushConstantRange push_constants_range = {
        .stageFlags = shader_stage_flags,
        .offset     = 0,
        .size       = push_constants_size,
    };

    bool push_constants_present = push_constants_size > 0;

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts            = set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(push_constants_present ? 1 : 0),
        .pPushConstantRanges    = push_constants_present ? &push_constants_range : nullptr
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout));

    VkPipelineRenderingCreateInfo pipeline_rendering_info = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext                   = nullptr,
        .viewMask                = 0,
        .colorAttachmentCount    = static_cast<uint32_t>(color_attachment_formats.size()),
        .pColorAttachmentFormats = color_attachment_formats.data(),
        .depthAttachmentFormat   = depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };

    VkVertexInputBindingDescription vertex_binding_description = {
        .binding   = 0,
        .stride    = sizeof(float) * 8,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    std::array<VkVertexInputAttributeDescription, 3> attribute_desriptions = {
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = 0,

        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = sizeof(float) * 3,
        },
        VkVertexInputAttributeDescription{
            .location = 2,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = sizeof(float) * 6,
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext                           = nullptr,
        .flags                           = 0,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &vertex_binding_description,
        .vertexAttributeDescriptionCount = attribute_desriptions.size(),
        .pVertexAttributeDescriptions    = attribute_desriptions.data()
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // Left to dynamic states
    VkViewport viewport = {.x = 0, .y = 0, .width = 0, .height = 0, .minDepth = 0.0f, .maxDepth = 1.0f};
    VkRect2D   scissor  = {.offset = {.x = 0, .y = 0}, .extent = {.width = 0, .height = 0}};

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext         = nullptr,
        .flags         = 0,
        .viewportCount = 1,
        .pViewports    = &viewport,
        .scissorCount  = 1,
        .pScissors     = &scissor
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable   = VK_FALSE,
        .minSampleShading      = 0.0f,
        .pSampleMask           = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable      = VK_FALSE
    };

    VkPipelineRasterizationStateCreateInfo rasterization_state = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext                   = nullptr,
        .flags                   = 0,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .cullMode                = VK_CULL_MODE_BACK_BIT,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp          = 0.0f,
        .depthBiasSlopeFactor    = 0.0f,
        .lineWidth               = 1.0f
    };

    std::vector<VkPipelineColorBlendAttachmentState> color_blend_attachment_states;
    for (VkFormat format : color_attachment_formats) {
        color_blend_attachment_states.push_back({
            .blendEnable         = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .colorBlendOp        = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp        = VK_BLEND_OP_ADD,
            .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        });
    }

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .logicOp         = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32_t>(color_blend_attachment_states.size()),
        .pAttachments    = color_blend_attachment_states.data(),
        .blendConstants  = {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext                 = nullptr,
        .flags                 = 0,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_FALSE,
        .depthCompareOp        = VK_COMPARE_OP_GREATER,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
        .front                 = {},
        .back                  = {},
        .minDepthBounds        = 0.0f,
        .maxDepthBounds        = 1.0f
    };

    std::vector<VkPipelineShaderStageCreateInfo> shader_stage_infos;
    for (auto& shader : shaders) {
        shader_stage_infos.push_back(
            VkPipelineShaderStageCreateInfo{
                .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext               = nullptr,
                .flags               = 0,
                .stage               = shader.stage,
                .module              = shader.module,
                .pName               = "main",
                .pSpecializationInfo = nullptr
            }
        );
    }

    std::vector<VkDynamicState> dynamic_states = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_info = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext             = nullptr,
        .flags             = 0,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
        .pDynamicStates    = dynamic_states.data()
    };

    VkGraphicsPipelineCreateInfo graphics_pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = &pipeline_rendering_info,
        .flags               = 0,
        .stageCount          = static_cast<uint32_t>(shader_stage_infos.size()),
        .pStages             = &shader_stage_infos[0],
        .pVertexInputState   = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pTessellationState  = nullptr,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState   = &multisample_state,
        .pDepthStencilState  = &depth_stencil_state,
        .pColorBlendState    = &color_blend_state,
        .pDynamicState       = &dynamic_state_info,
        .layout              = pipeline_layout,
        .renderPass          = VK_NULL_HANDLE,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, &pipeline));

    return Pipeline{
        .shaders         = shaders,
        .stage_flags     = shader_stage_flags,
        .set_layouts     = descriptor_sets,
        .layout_handles  = set_layouts,
        .pipeline_layout = pipeline_layout,
        .pipeline_handle = pipeline,
        .bind_point      = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };
}

Shader shader_from_file(VkDevice device, VkShaderStageFlagBits stage, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::in | std::ios::ate);

    if (file.is_open()) {
        size_t                 length = static_cast<size_t>(file.tellg());
        std::vector<std::byte> buffer(length);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), length);

        VkShaderModuleCreateInfo module_info = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext    = nullptr,
            .flags    = 0,
            .codeSize = buffer.size(),
            .pCode    = reinterpret_cast<const uint32_t*>(buffer.data())
        };

        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &module_info, nullptr, &module));

        return Shader{.module = module, .stage = stage};
    }

    spdlog::error("Failed to load shader module {}", path.filename().string());
    return {};
}

std::vector<VkDescriptorSet>
allocate_descriptor_sets(VkDevice device, VkDescriptorPool descriptor_pool, const Pipeline& pipeline) {
    if (pipeline.layout_handles.size() == 1) {
        if (pipeline.set_layouts[0].is_push_set) {
            return {};
        }
    }

    VkDescriptorSetAllocateInfo set_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext              = nullptr,
        .descriptorPool     = descriptor_pool,
        .descriptorSetCount = static_cast<uint32_t>(pipeline.layout_handles.size()),
        .pSetLayouts        = pipeline.layout_handles.data()
    };

    std::vector<VkDescriptorSet> descriptor_sets(pipeline.layout_handles.size());
    VK_CHECK(vkAllocateDescriptorSets(device, &set_info, &descriptor_sets[0]));

    for (int i = 0; i < pipeline.set_layouts.size(); i++) {
        std::vector<VkWriteDescriptorSet> write_sets;

        std::vector<VkAccelerationStructureKHR>                   acceleration_structures;
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> acceleration_write_sets;

        uint32_t binding_idx = 0;
        for (auto& desc : pipeline.set_layouts[i].bindings) {
            if (desc.write_info.type != DescriptorType::NONE) {
                VkWriteDescriptorSet write_set = {
                    .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .pNext           = nullptr,
                    .dstSet          = descriptor_sets[i],
                    .dstBinding      = binding_idx,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType  = desc.type,
                };

                if (desc.write_info.type == DescriptorType::IMAGE) {
                    write_set.pImageInfo = &desc.write_info.image_info;
                }

                if (desc.write_info.type == DescriptorType::BUFFER) {
                    write_set.pBufferInfo = &desc.write_info.buffer_info;
                }

                if (desc.write_info.type == DescriptorType::ACCELERATION_STRUCTURE) {
                    acceleration_structures.push_back(desc.write_info.acceleration_structure);
                    acceleration_write_sets.push_back({
                        .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                        .pNext                      = nullptr,
                        .accelerationStructureCount = 1,
                        .pAccelerationStructures    = &acceleration_structures[acceleration_structures.size() - 1],
                    });
                    write_set.pNext = &acceleration_write_sets[acceleration_write_sets.size() - 1];
                }

                write_sets.push_back(write_set);
            }
            binding_idx++;
        }

        if (write_sets.size() > 0) {
            vkUpdateDescriptorSets(device, write_sets.size(), write_sets.data(), 0, nullptr);
        }
    }

    return descriptor_sets;
}

Pipeline create_compute_pipeline(
    VkDevice                             device,
    const Shader&                        shader,
    const std::vector<DescriptorLayout>& descriptor_sets,
    uint32_t                             push_constants_size,
    VkDescriptorSetLayout                additional_set
) {
    VkShaderStageFlags shader_stage_flags = (VkShaderStageFlags)shader.stage;

    std::vector<VkDescriptorSetLayout> set_layouts;
    for (auto& set : descriptor_sets) {
        set_layouts.push_back(create_descriptor_set_layout(device, shader_stage_flags, set.bindings, set.is_push_set));
    }

    VkPushConstantRange push_constants_range = {
        .stageFlags = shader_stage_flags,
        .offset     = 0,
        .size       = push_constants_size,
    };

    bool push_constants_present = push_constants_size > 0;

    if (additional_set != VK_NULL_HANDLE) {
        set_layouts.push_back(additional_set);
    }

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = static_cast<uint32_t>(set_layouts.size()),
        .pSetLayouts            = set_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(push_constants_present ? 1 : 0),
        .pPushConstantRanges    = push_constants_present ? &push_constants_range : nullptr
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout));

    // TODO: this is ugly as sin
    if (additional_set != VK_NULL_HANDLE) {
        set_layouts.pop_back();
    }

    VkPipelineShaderStageCreateInfo shader_stage_info = {
        .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext               = nullptr,
        .flags               = 0,
        .stage               = shader.stage,
        .module              = shader.module,
        .pName               = "main",
        .pSpecializationInfo = nullptr
    };

    VkComputePipelineCreateInfo pipeline_info = {
        .sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .stage              = shader_stage_info,
        .layout             = pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex  = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

    return Pipeline{
        .shaders         = {shader},
        .stage_flags     = shader_stage_flags,
        .set_layouts     = descriptor_sets,
        .layout_handles  = set_layouts,
        .pipeline_layout = pipeline_layout,
        .pipeline_handle = pipeline,
        .bind_point      = VK_PIPELINE_BIND_POINT_COMPUTE,
    };
}
void destroy_pipeline(VkDevice device, const Pipeline& pipeline) {
    for (auto shader : pipeline.shaders) {
        vkDestroyShaderModule(device, shader.module, nullptr);
    }

    for (auto layout : pipeline.layout_handles) {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
    }

    vkDestroyPipelineLayout(device, pipeline.pipeline_layout, nullptr);
    vkDestroyPipeline(device, pipeline.pipeline_handle, nullptr);
}
