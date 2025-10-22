#include "rt_scene.hpp"

RTScene create_rt_scene(
    VkDevice                         device,
    VkPhysicalDevice                 physical_device,
    VmaAllocator                     allocator,
    VkCommandBuffer                  command_buffer,
    VkQueue                          queue,
    const std::vector<Mesh>&         meshes,
    const std::vector<MeshInstance>& mesh_instances,
    VkDeviceAddress                  global_vertex_buffer_address,
    VkDeviceAddress                  global_index_buffer_address
) {
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
    };
    VkPhysicalDeviceProperties2 physical_device_properties_2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &ray_tracing_properties,
    };
    vkGetPhysicalDeviceProperties2(physical_device, &physical_device_properties_2);

    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
    };
    VkPhysicalDeviceFeatures2 physical_device_features_2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &acceleration_structure_features,
    };
    vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_features_2);

    spdlog::info("max recursion depth: {}", ray_tracing_properties.maxRayRecursionDepth);

    std::vector<BLAS> bottom_level_acceleration_structures;

    for (auto& mesh : meshes) {
        VkAccelerationStructureGeometryKHR acceleration_structure_geometry = {
            .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .pNext        = nullptr,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry =
                {.triangles =
                     {
                         .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                         .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                         .vertexData =
                             {
                                 .deviceAddress = global_vertex_buffer_address + (mesh.vertex_buffer_offset),
                             },
                         .vertexStride = sizeof(Vertex),
                         .maxVertex    = mesh.vertex_count - 1,
                         .indexType    = VK_INDEX_TYPE_UINT32,
                         .indexData =
                             {
                                 .deviceAddress = global_index_buffer_address + (mesh.index_buffer_offset),
                             },
                         .transformData = {},
                     }},
            .flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR,
        };

        VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info = {
            .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext         = nullptr,
            .type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .geometryCount = 1,
            .pGeometries   = &acceleration_structure_geometry,
        };

        const uint32_t primitive_count = mesh.index_count / 3;

        VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        };
        vkGetAccelerationStructureBuildSizesKHR(
            device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &acceleration_structure_build_geometry_info,
            &primitive_count,
            &acceleration_structure_build_sizes_info
        );

        Buffer bottom_level_acceleration_structure_buffer = create_buffer(
            acceleration_structure_build_sizes_info.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            allocator,
            VMA_MEMORY_USAGE_GPU_ONLY
        );

        VkAccelerationStructureCreateInfoKHR acceleration_structure_create_info = {
            .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .buffer = bottom_level_acceleration_structure_buffer.handle,
            .size   = acceleration_structure_build_sizes_info.accelerationStructureSize,
            .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        };

        VkAccelerationStructureKHR bottom_level_acceleration_structure = VK_NULL_HANDLE;
        vkCreateAccelerationStructureKHR(
            device, &acceleration_structure_create_info, nullptr, &bottom_level_acceleration_structure
        );

        Buffer scratch_buffer = create_buffer(
            acceleration_structure_build_sizes_info.buildScratchSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            allocator,
            VMA_MEMORY_USAGE_GPU_ONLY
        );
        VkDeviceAddress scratch_buffer_address = get_buffer_device_address(scratch_buffer, device);

        VkAccelerationStructureBuildGeometryInfoKHR acceleration_build_geometry_info = {
            .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .type                     = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags                    = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .dstAccelerationStructure = bottom_level_acceleration_structure,
            .geometryCount            = 1,
            .pGeometries              = &acceleration_structure_geometry,
            .scratchData              = {.deviceAddress = scratch_buffer_address},
        };

        VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info = {
            .primitiveCount  = primitive_count,
            .primitiveOffset = 0,
            .firstVertex     = 0,
            .transformOffset = 0,
        };

        std::vector<VkAccelerationStructureBuildRangeInfoKHR*> acceleration_build_structure_range_infos = {
            &acceleration_structure_build_range_info
        };

        VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = nullptr,
            .flags            = 0,
            .pInheritanceInfo = nullptr
        };
        VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

        vkCmdBuildAccelerationStructuresKHR(
            command_buffer, 1, &acceleration_build_geometry_info, acceleration_build_structure_range_infos.data()
        );

        VK_CHECK(vkEndCommandBuffer(command_buffer));

        VkSubmitInfo submit_info = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = nullptr,
            .waitSemaphoreCount   = 0,
            .pWaitSemaphores      = nullptr,
            .pWaitDstStageMask    = nullptr,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &command_buffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores    = nullptr
        };
        VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
        VK_CHECK(vkDeviceWaitIdle(device));

        destroy_buffer(scratch_buffer, device, allocator);

        VkAccelerationStructureDeviceAddressInfoKHR acceleration_device_address_info = {
            .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .accelerationStructure = bottom_level_acceleration_structure,
        };

        VkDeviceAddress bottom_level_acceleration_structure_address =
            vkGetAccelerationStructureDeviceAddressKHR(device, &acceleration_device_address_info);

        bottom_level_acceleration_structures.push_back(
            {.buffer  = bottom_level_acceleration_structure_buffer,
             .handle  = bottom_level_acceleration_structure,
             .address = bottom_level_acceleration_structure_address}
        );
    }

    std::vector<VkAccelerationStructureInstanceKHR> tlas_instances;
    for (int i = 0; i < mesh_instances.size(); i++) {
        const MeshInstance& inst = mesh_instances[i];

        glm::mat4 model = glm::mat4(1.0f);

        VkAccelerationStructureInstanceKHR instance = {
            .transform                              = {},
            .instanceCustomIndex                    = static_cast<uint32_t>(i),
            .mask                                   = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags                                  = 0,
            .accelerationStructureReference         = bottom_level_acceleration_structures[inst.mesh_id].address
        };

        glm::mat3 transform = transpose(glm::mat3_cast(inst.rotation)) * inst.scale;
        memcpy(instance.transform.matrix[0], &transform[0], sizeof(float) * 3);
        memcpy(instance.transform.matrix[1], &transform[1], sizeof(float) * 3);
        memcpy(instance.transform.matrix[2], &transform[2], sizeof(float) * 3);
        instance.transform.matrix[0][3] = inst.position.x;
        instance.transform.matrix[1][3] = inst.position.y;
        instance.transform.matrix[2][3] = inst.position.z;

        tlas_instances.push_back(instance);
    }

    Buffer instance_buffer = create_buffer(
        tlas_instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );
    VkDeviceAddress instance_buffer_address = get_buffer_device_address(instance_buffer, device);

    void* instance_buffer_ptr = nullptr;
    VK_CHECK(vmaMapMemory(allocator, instance_buffer.allocation, &instance_buffer_ptr));
    memcpy(
        instance_buffer_ptr, tlas_instances.data(), tlas_instances.size() * sizeof(VkAccelerationStructureInstanceKHR)
    );

    VkAccelerationStructureGeometryInstancesDataKHR instances_data = {
        .sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
        .data            = {
                       .deviceAddress = instance_buffer_address,
        }
    };

    VkAccelerationStructureGeometryKHR tlas_geometry = {
        .sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry     = {
                .instances = instances_data,
        }
    };

    VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info = {
        .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries   = &tlas_geometry
    };

    VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    uint32_t instance_count = static_cast<uint32_t>(tlas_instances.size());
    vkGetAccelerationStructureBuildSizesKHR(
        device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &tlas_build_info,
        &instance_count,
        &acceleration_structure_build_sizes_info
    );

    Buffer top_level_acceleration_structure_buffer = create_buffer(
        acceleration_structure_build_sizes_info.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        allocator,
        VMA_MEMORY_USAGE_GPU_ONLY
    );

    VkAccelerationStructureCreateInfoKHR acceleration_structure_create_info = {
        .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = top_level_acceleration_structure_buffer.handle,
        .size   = acceleration_structure_build_sizes_info.accelerationStructureSize,
        .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };

    TLAS top_level_acceleration_structure;
    vkCreateAccelerationStructureKHR(
        device, &acceleration_structure_create_info, nullptr, &top_level_acceleration_structure.handle
    );

    Buffer scratch_buffer = create_buffer(
        acceleration_structure_build_sizes_info.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        allocator,
        VMA_MEMORY_USAGE_GPU_ONLY
    );
    VkDeviceAddress scratch_buffer_address = get_buffer_device_address(scratch_buffer, device);

    VkAccelerationStructureBuildGeometryInfoKHR acceleration_build_geometry_info = {
        .sType                    = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type                     = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags                    = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .dstAccelerationStructure = top_level_acceleration_structure.handle,
        .geometryCount            = 1,
        .pGeometries              = &tlas_geometry,
        .scratchData              = {.deviceAddress = scratch_buffer_address},
    };

    VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info = {
        .primitiveCount  = static_cast<uint32_t>(tlas_instances.size()),
        .primitiveOffset = 0,
        .firstVertex     = 0,
        .transformOffset = 0,
    };

    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> acceleration_build_structure_range_infos = {
        &acceleration_structure_build_range_info
    };

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .pNext = nullptr, .flags = 0, .pInheritanceInfo = nullptr
    };
    VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

    vkCmdBuildAccelerationStructuresKHR(
        command_buffer, 1, &acceleration_build_geometry_info, acceleration_build_structure_range_infos.data()
    );

    VK_CHECK(vkEndCommandBuffer(command_buffer));

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = nullptr,
        .waitSemaphoreCount   = 0,
        .pWaitSemaphores      = nullptr,
        .pWaitDstStageMask    = nullptr,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores    = nullptr
    };
    VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    VK_CHECK(vkDeviceWaitIdle(device));

    destroy_buffer(scratch_buffer, device, allocator);
    destroy_buffer(instance_buffer, device, allocator);

    VkDeviceAddress top_level_acceleration_structure_address;

    VkAccelerationStructureDeviceAddressInfoKHR acceleration_device_address_info{
        .sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = top_level_acceleration_structure.handle,
    };

    top_level_acceleration_structure_address =
        vkGetAccelerationStructureDeviceAddressKHR(device, &acceleration_device_address_info);
    top_level_acceleration_structure.buffer  = top_level_acceleration_structure_buffer;
    top_level_acceleration_structure.address = top_level_acceleration_structure_address;

    std::vector<VkPipelineShaderStageCreateInfo>      shader_stages;
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;

    auto load_shader =
        [device](const std::filesystem::path& path, VkShaderStageFlagBits stages) -> VkPipelineShaderStageCreateInfo {
        auto module = shader_module_from_file(device, path);

        return {
            .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext               = nullptr,
            .flags               = 0,
            .stage               = stages,
            .module              = module,
            .pName               = "main",
            .pSpecializationInfo = nullptr,
        };
    };

    RTScene scene = {
        .blas_instances                  = bottom_level_acceleration_structures,
        .tlas                            = top_level_acceleration_structure,
        .rt_properties                   = ray_tracing_properties,
        .acceleration_structure_features = acceleration_structure_features,
    };

    return scene;
}

void destroy_rt_scene(const RTScene& scene, VkDevice device, VmaAllocator allocator) {
    for (auto structure : scene.blas_instances) {
        vkDestroyAccelerationStructureKHR(device, structure.handle, nullptr);
        destroy_buffer(structure.buffer, device, allocator);
    }
    vkDestroyAccelerationStructureKHR(device, scene.tlas.handle, nullptr);
    destroy_buffer(scene.tlas.buffer, device, allocator);
}
