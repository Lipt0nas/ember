#include "shader.hpp"

Shader2 load_shader(VkDevice device, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::in | std::ios::ate);

    if (file.is_open()) {
        size_t                 length = static_cast<size_t>(file.tellg());
        std::vector<std::byte> buffer(length);

        file.seekg(0);
        file.read(reinterpret_cast<char*>(buffer.data()), length);

        SpvReflectShaderModule module;
        SpvReflectResult       result = spvReflectCreateShaderModule(buffer.size(), buffer.data(), &module);
        if (result != SPV_REFLECT_RESULT_SUCCESS) {
            spdlog::error("Failed to reflect shader module");
        }

        Shader2 shader = {
            .stage = static_cast<VkShaderStageFlagBits>(module.shader_stage),
        };

        uint32_t count = 0;

        result = spvReflectEnumerateDescriptorSets(&module, &count, NULL);
        std::vector<SpvReflectDescriptorSet*> sets(count);
        result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());

        // spdlog::info("Descriptor sets: ");
        // NOTE: Only using one set for now, once bindless textures are difficult to implement
        for (int i = 0; i < glm::min(sets.size(), 1ul); i++) {
            const SpvReflectDescriptorSet& reflected_set = *sets[i];

            for (int j = 0; j < reflected_set.binding_count; j++) {
                const SpvReflectDescriptorBinding& reflected_binding = *reflected_set.bindings[j];

                if (reflected_binding.set != 0) {
                    continue;
                }

                shader.layout_bindings.push_back(
                    DescriptorBinding{
                        .set     = reflected_binding.set,
                        .binding = reflected_binding.binding,
                        .type    = static_cast<VkDescriptorType>(reflected_binding.descriptor_type)
                    }
                );
            }
        }

        result = spvReflectEnumerateDescriptorBindings(&module, &count, NULL);
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        result = spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());

        result = spvReflectEnumerateInterfaceVariables(&module, &count, NULL);
        std::vector<SpvReflectInterfaceVariable*> interface_variables(count);
        result = spvReflectEnumerateInterfaceVariables(&module, &count, interface_variables.data());

        result = spvReflectEnumerateInputVariables(&module, &count, NULL);
        std::vector<SpvReflectInterfaceVariable*> input_variables(count);
        result = spvReflectEnumerateInputVariables(&module, &count, input_variables.data());

        result = spvReflectEnumerateOutputVariables(&module, &count, NULL);
        std::vector<SpvReflectInterfaceVariable*> output_variables(count);
        result = spvReflectEnumerateOutputVariables(&module, &count, output_variables.data());

        result = spvReflectEnumeratePushConstantBlocks(&module, &count, NULL);
        std::vector<SpvReflectBlockVariable*> push_constant(count);
        result = spvReflectEnumeratePushConstantBlocks(&module, &count, push_constant.data());

        result = spvReflectEnumerateSpecializationConstants(&module, &count, NULL);
        std::vector<SpvReflectSpecializationConstant*> spec_constant(count);
        result = spvReflectEnumerateSpecializationConstants(&module, &count, spec_constant.data());

        if (module.entry_point_count > 1) {
            spdlog::warn("Multiple entry points present, will only use the first one");
        }

        auto entry_point_info   = module.entry_points[0];
        shader.workgroup_size_x = entry_point_info.local_size.x;
        shader.workgroup_size_y = entry_point_info.local_size.y;
        shader.workgroup_size_z = entry_point_info.local_size.z;

        VkShaderModuleCreateInfo module_info = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext    = nullptr,
            .flags    = 0,
            .codeSize = buffer.size(),
            .pCode    = reinterpret_cast<const uint32_t*>(buffer.data())
        };

        VK_CHECK(vkCreateShaderModule(device, &module_info, nullptr, &shader.module));

        spvReflectDestroyShaderModule(&module);

        return shader;
    }

    spdlog::error("Failed to load shader module {}", path.filename().string());
    return Shader2{
        .module = VK_NULL_HANDLE,
    };
}

Program create_shader_program(
    VkDevice                    device,
    const std::vector<Shader2>& shaders,
    VkPipelineBindPoint         bind_point,
    uint32_t                    push_constants_size,
    VkDescriptorSetLayout       additional_layout,
    VkDescriptorPool            allocation_pool
) {
    std::vector<VkDescriptorSetLayoutBinding>    vk_bindings;
    std::vector<VkDescriptorUpdateTemplateEntry> template_entries;
    std::vector<VkDescriptorType>                descriptor_types;

    VkShaderStageFlags stage_flags = 0;

    uint32_t workgroup_size_x = 1;
    uint32_t workgroup_size_y = 1;
    uint32_t workgroup_size_z = 1;

    for (auto& shader : shaders) {
        stage_flags |= shader.stage;

        workgroup_size_x = glm::max(shader.workgroup_size_x, workgroup_size_x);
        workgroup_size_y = glm::max(shader.workgroup_size_y, workgroup_size_y);
        workgroup_size_z = glm::max(shader.workgroup_size_z, workgroup_size_z);
    }

    int descriptor_idx = 0;
    for (auto& shader : shaders) {
        for (auto& binding : shader.layout_bindings) {
            spdlog::info(
                "{} -> {}: {} ({})",
                string_VkShaderStageFlagBits(shader.stage),
                binding.set,
                binding.binding,
                string_VkDescriptorType(binding.type)
            );
            bool binding_exists = false;
            for (auto& existing_binding : vk_bindings) {
                if (binding.binding == existing_binding.binding) {
                    if (binding.type == existing_binding.descriptorType) {
                        existing_binding.stageFlags |= shader.stage;
                        binding_exists = true;
                        break;
                    } else {
                        spdlog::warn(
                            "Missmatch binding type at index {}: {} vs {}",
                            descriptor_idx,
                            string_VkDescriptorType(existing_binding.descriptorType),
                            string_VkDescriptorType(binding.type)
                        );
                        break;
                    }
                }
            }

            if (!binding_exists) {
                vk_bindings.push_back(
                    VkDescriptorSetLayoutBinding{
                        .binding            = binding.binding,
                        .descriptorType     = binding.type,
                        .descriptorCount    = 1,
                        .stageFlags         = shader.stage,
                        .pImmutableSamplers = nullptr
                    }
                );

                template_entries.push_back(
                    VkDescriptorUpdateTemplateEntry{
                        .dstBinding      = binding.binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType  = binding.type,
                        .offset          = sizeof(DescriptorInfo) * descriptor_idx++,
                        .stride          = sizeof(DescriptorInfo),
                    }
                );

                descriptor_types.push_back(binding.type);
            }
        }
    }

    // for (auto binding : vk_bindings) {
    //     spdlog::info(
    //         "\n\t\tBinding: {}\n\t\tType: {}\n\t\t{}",
    //         binding.binding,
    //         string_VkDescriptorType(static_cast<VkDescriptorType>(binding.descriptorType)),
    //         string_VkShaderStageFlags(binding.stageFlags)
    //     );
    // }

    // for (auto templt : template_entries) {
    //     spdlog::info(
    //         "\n\t\tBinding: {}\n\t\tType: {}\n\t\t{}",
    //         templt.dstBinding,
    //         string_VkDescriptorType(static_cast<VkDescriptorType>(templt.descriptorType)),
    //         templt.offset / sizeof(DescriptorInfo)
    //     );
    // }

    std::vector<VkDescriptorBindingFlags> flags;
    for (auto& b : vk_bindings) {
        flags.push_back(VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT);
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .pNext         = nullptr,
        .bindingCount  = static_cast<uint32_t>(vk_bindings.size()),
        .pBindingFlags = flags.data()
    };

    VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = allocation_pool == VK_NULL_HANDLE ? nullptr : &flags_info,
        .flags        = allocation_pool == VK_NULL_HANDLE ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT
                                                          : VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<uint32_t>(vk_bindings.size()),
        .pBindings    = vk_bindings.data()
    };

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &descriptor_layout_info, nullptr, &descriptor_set_layout));

    VkPushConstantRange push_constants_range = {
        .stageFlags = stage_flags,
        .offset     = 0,
        .size       = push_constants_size,
    };

    bool push_constants_present = (push_constants_size > 0);

    VkDescriptorSetLayout layouts[] = {descriptor_set_layout, additional_layout};

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext                  = nullptr,
        .flags                  = 0,
        .setLayoutCount         = static_cast<uint32_t>(additional_layout == VK_NULL_HANDLE ? 1 : 2),
        .pSetLayouts            = layouts,
        .pushConstantRangeCount = static_cast<uint32_t>(push_constants_present ? 1 : 0),
        .pPushConstantRanges    = push_constants_present ? &push_constants_range : nullptr
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_info, nullptr, &pipeline_layout));

    VkDescriptorUpdateTemplateCreateInfo update_template_info = {
        .sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
        .pNext                      = nullptr,
        .flags                      = 0,
        .descriptorUpdateEntryCount = static_cast<uint32_t>(template_entries.size()),
        .pDescriptorUpdateEntries   = template_entries.data(),
        .templateType               = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS,
        .descriptorSetLayout        = descriptor_set_layout,
        .pipelineBindPoint          = bind_point,
        .pipelineLayout             = pipeline_layout,
        .set                        = 0
    };

    VkDescriptorUpdateTemplate update_template = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorUpdateTemplate(device, &update_template_info, 0, &update_template));

    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    if (allocation_pool != VK_NULL_HANDLE) {

        VkDescriptorSetAllocateInfo allocate_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = nullptr,
            .descriptorPool     = allocation_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &descriptor_set_layout
        };
        VK_CHECK(vkAllocateDescriptorSets(device, &allocate_info, &descriptor_set));
    }

    return Program{
        .shaders                    = shaders,
        .descriptor_set_layout      = descriptor_set_layout,
        .descriptor_update_template = update_template,
        .bind_point                 = bind_point,
        .pipeline_layout            = pipeline_layout,
        .push_constants_size        = push_constants_size,
        .stages                     = stage_flags,
        .workgroup_size_x           = workgroup_size_x,
        .workgroup_size_y           = workgroup_size_y,
        .workgroup_size_z           = workgroup_size_z,
        .descriptor_types           = descriptor_types,
        .allocated_set              = descriptor_set,
        .additional_layout          = additional_layout,
    };
}

VkPipeline sp_create_compute_pipeline(VkDevice device, const Program& program) {
    if (program.shaders.size() != 1) {
        spdlog::error("Program contains more than one shader when creating a compute pipeline");
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo shader_stage_info = {
        .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext               = nullptr,
        .flags               = 0,
        .stage               = program.shaders[0].stage,
        .module              = program.shaders[0].module,
        .pName               = "main",
        .pSpecializationInfo = nullptr
    };

    VkComputePipelineCreateInfo pipeline_info = {
        .sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext              = nullptr,
        .flags              = 0,
        .stage              = shader_stage_info,
        .layout             = program.pipeline_layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex  = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline));

    return pipeline;
}

void dispatch(
    VkDevice                           device,
    VkCommandBuffer                    command_buffer,
    const Program&                     program,
    uint32_t                           thread_count_x,
    uint32_t                           thread_count_y,
    uint32_t                           thread_count_z,
    const std::vector<DescriptorInfo>& descriptors,
    const void*                        push_constants,
    VkDescriptorSet                    additional_set
) {
    bind_program(device, command_buffer, program, descriptors, push_constants, additional_set);

    uint32_t dispatch_x = (thread_count_x + program.workgroup_size_x - 1) / program.workgroup_size_x;
    uint32_t dispatch_y = (thread_count_y + program.workgroup_size_y - 1) / program.workgroup_size_y;

    vkCmdDispatch(command_buffer, dispatch_x, dispatch_y, 1);
}

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
    VkDescriptorSet                        additional_set
) {
    bind_program(device, command_buffer, program, descriptors, push_constants, additional_set);

    vkCmdTraceRaysKHR(command_buffer, raygen_sbt, miss_sbt, hit_sbt, callable_sbt, width, height, depth);
}

void destroy_shader(VkDevice device, const Shader2& shader) {
    vkDestroyShaderModule(device, shader.module, nullptr);
}

void destroy_shader_program(VkDevice device, const Program& program) {
    for (auto& shader : program.shaders) {
        destroy_shader(device, shader);
    }

    vkDestroyDescriptorUpdateTemplate(device, program.descriptor_update_template, nullptr);
    vkDestroyDescriptorSetLayout(device, program.descriptor_set_layout, nullptr);

    vkDestroyPipelineLayout(device, program.pipeline_layout, nullptr);
}

// TODO: assumes a very rigid shader layout
VkPipeline create_ray_tracing_pipeline(VkDevice device, const Program& program) {
    std::vector<VkPipelineShaderStageCreateInfo>      shader_stages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

    auto shader_info = [device](const Shader2& shader) -> VkPipelineShaderStageCreateInfo {
        return {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = shader.stage,
            .module              = shader.module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        };
    };

    // Ray generation group
    {
        shader_stages.push_back(shader_info(program.shaders[0]));

        VkRayTracingShaderGroupCreateInfoKHR raygen_group_ci = {};
        raygen_group_ci.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        raygen_group_ci.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        raygen_group_ci.generalShader      = static_cast<uint32_t>(shader_stages.size()) - 1;
        raygen_group_ci.closestHitShader   = VK_SHADER_UNUSED_KHR;
        raygen_group_ci.anyHitShader       = VK_SHADER_UNUSED_KHR;
        raygen_group_ci.intersectionShader = VK_SHADER_UNUSED_KHR;

        shader_groups.push_back(raygen_group_ci);
    }

    // Ray miss group
    {
        shader_stages.push_back(shader_info(program.shaders[1]));

        VkRayTracingShaderGroupCreateInfoKHR miss_group_ci = {};
        miss_group_ci.sType                                = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        miss_group_ci.type                                 = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        miss_group_ci.generalShader                        = static_cast<uint32_t>(shader_stages.size()) - 1;
        miss_group_ci.closestHitShader                     = VK_SHADER_UNUSED_KHR;
        miss_group_ci.anyHitShader                         = VK_SHADER_UNUSED_KHR;
        miss_group_ci.intersectionShader                   = VK_SHADER_UNUSED_KHR;

        shader_groups.push_back(miss_group_ci);
    }

    // Ray closest hit group
    {
        shader_stages.push_back(shader_info(program.shaders[2]));

        VkRayTracingShaderGroupCreateInfoKHR closest_hit_group_ci = {};
        closest_hit_group_ci.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        closest_hit_group_ci.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        closest_hit_group_ci.generalShader      = VK_SHADER_UNUSED_KHR;
        closest_hit_group_ci.closestHitShader   = static_cast<uint32_t>(shader_stages.size()) - 1;
        closest_hit_group_ci.anyHitShader       = VK_SHADER_UNUSED_KHR;
        closest_hit_group_ci.intersectionShader = VK_SHADER_UNUSED_KHR;

        if (program.shaders.size() > 3) {
            shader_stages.push_back(shader_info(program.shaders[3]));
            closest_hit_group_ci.anyHitShader = static_cast<uint32_t>(shader_stages.size()) - 1;
        }

        shader_groups.push_back(closest_hit_group_ci);
    }

    VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_create_info = {};
    raytracing_pipeline_create_info.sType      = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    raytracing_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    raytracing_pipeline_create_info.pStages    = shader_stages.data();
    raytracing_pipeline_create_info.groupCount = static_cast<uint32_t>(shader_groups.size());
    raytracing_pipeline_create_info.pGroups    = shader_groups.data();
    raytracing_pipeline_create_info.maxPipelineRayRecursionDepth = 3;
    raytracing_pipeline_create_info.layout                       = program.pipeline_layout;

    VkPipeline rt_pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateRayTracingPipelinesKHR(
        device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &raytracing_pipeline_create_info, nullptr, &rt_pipeline
    ));

    return rt_pipeline;
}

VkPipeline
sp_create_graphics_pipeline(VkDevice device, const Program& program, VkPipelineRenderingCreateInfo rendering_info) {
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
    for (int i = 0; i < rendering_info.colorAttachmentCount; i++) {
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
    for (auto& shader : program.shaders) {
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
        .pNext               = &rendering_info,
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
        .layout              = program.pipeline_layout,
        .renderPass          = VK_NULL_HANDLE,
        .subpass             = 0,
        .basePipelineHandle  = VK_NULL_HANDLE,
        .basePipelineIndex   = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_pipeline_info, nullptr, &pipeline));

    return pipeline;
}

void bind_program(
    VkDevice                           device,
    VkCommandBuffer                    command_buffer,
    const Program&                     program,
    const std::vector<DescriptorInfo>& descriptors,
    const void*                        push_constants,
    VkDescriptorSet                    additional_set
) {
    if (program.push_constants_size > 0 && push_constants) {
        vkCmdPushConstants(
            command_buffer, program.pipeline_layout, program.stages, 0, program.push_constants_size, push_constants
        );
    }

    if (descriptors.size() > 0 && program.allocated_set == VK_NULL_HANDLE) {
        vkCmdPushDescriptorSetWithTemplate(
            command_buffer, program.descriptor_update_template, program.pipeline_layout, 0, descriptors.data()
        );
    } else if (descriptors.size() > 0 && program.allocated_set != VK_NULL_HANDLE) {
        std::vector<VkWriteDescriptorSet> write_sets;

        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> acceleration_writes;

        uint32_t dst_binding = 0;
        for (auto& descriptor_info : descriptors) {
            auto type = descriptor_info.type;

            const VkDescriptorImageInfo*  pImageInfo;
            const VkDescriptorBufferInfo* pBufferInfo;
            const VkBufferView*           pTexelBufferView;

            VkWriteDescriptorSet set = {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .pNext           = nullptr,
                .dstSet          = program.allocated_set,
                .dstBinding      = dst_binding,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType  = program.descriptor_types[dst_binding]
            };

            switch (type) {
            case DescriptorType::IMAGE:
                set.pImageInfo = &descriptor_info.image_info;
                break;
            case DescriptorType::BUFFER:
                set.pBufferInfo = &descriptor_info.buffer_info;
                break;
            case DescriptorType::ACCELERATION_STRUCTURE:
                acceleration_writes.push_back({
                    .sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                    .accelerationStructureCount = 1,
                    .pAccelerationStructures    = &descriptor_info.acceleration_structure_info,
                });

                set.pNext = &acceleration_writes[acceleration_writes.size() - 1];
                break;
            };

            write_sets.push_back(set);
            dst_binding++;
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(write_sets.size()), write_sets.data(), 0, nullptr);
        vkCmdBindDescriptorSets(
            command_buffer, program.bind_point, program.pipeline_layout, 0, 1, &program.allocated_set, 0, nullptr
        );
    }

    if (program.additional_layout != VK_NULL_HANDLE && additional_set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(
            command_buffer, program.bind_point, program.pipeline_layout, 1, 1, &additional_set, 0, nullptr
        );
    }
}
