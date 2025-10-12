#pragma once

#include "ember.hpp"
#include "pipeline.hpp"
#include "resources.hpp"

#include <set>

// TODO: move this out
struct Material {
    uint32_t albedo_index;
    uint32_t normals_index;
    uint32_t material_index;
};

// TODO: move this out
struct Mesh {
    VkDeviceSize vertex_buffer_offset;
    VkDeviceSize index_buffer_offset;

    uint32_t meshlet_offset;
    uint32_t meshlet_count;

    uint32_t vertex_count;
    uint32_t index_count;

    Material material;

    glm::vec3 center = {};
    float     radius = 0.0f;

    glm::vec3 position = {};
    float     scale    = 1.0f;
};

// TODO: move this out
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    bool operator==(const Vertex& other) const {
        return position == other.position && normal == other.normal && uv == other.uv;
    }
};

struct TLAS {
    Buffer buffer;

    VkAccelerationStructureKHR handle;
    VkDeviceAddress            address;
};

struct BLAS {
    Buffer buffer;

    VkAccelerationStructureKHR handle;
    VkDeviceAddress            address;
};

struct RTScene {
    std::vector<BLAS> blas_instances;
    TLAS              tlas;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR  rt_properties;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features;

    VkPipeline pipeline;
    Buffer     raygen_shader_binding_table;
    Buffer     miss_shader_binding_table;
    Buffer     hit_shader_binding_table;

    uint32_t                        handle_size_aligned;
    VkStridedDeviceAddressRegionKHR raygen_shader_sbt_entry;
    VkStridedDeviceAddressRegionKHR miss_shader_sbt_entry;
    VkStridedDeviceAddressRegionKHR hit_shader_sbt_entry;
};

bool is_mesh_same(const Mesh& m1, const Mesh& m2) {
    return (
        (m1.index_buffer_offset == m2.index_buffer_offset) && (m1.vertex_buffer_offset == m2.vertex_buffer_offset) &&
        (m1.vertex_count == m2.vertex_count) && (m1.index_count == m2.index_count)
    );
}

VkTransformMatrixKHR glm_to_vk_transform(const glm::mat4& mat) {
    VkTransformMatrixKHR transform;
    transform.matrix[0][0] = mat[0][0];
    transform.matrix[0][1] = mat[1][0];
    transform.matrix[0][2] = mat[2][0];
    transform.matrix[0][3] = mat[3][0];

    transform.matrix[1][0] = mat[0][1];
    transform.matrix[1][1] = mat[1][1];
    transform.matrix[1][2] = mat[2][1];
    transform.matrix[1][3] = mat[3][1];

    transform.matrix[2][0] = mat[0][2];
    transform.matrix[2][1] = mat[1][2];
    transform.matrix[2][2] = mat[2][2];
    transform.matrix[2][3] = mat[3][2];

    return transform;
}

RTScene create_rt_scene(
    VkDevice                     device,
    VkPhysicalDevice             physical_device,
    VmaAllocator                 allocator,
    VkCommandBuffer              command_buffer,
    VkQueue                      queue,
    const std::vector<Mesh>&     meshes,
    VkDeviceAddress              global_vertex_buffer_address,
    VkDeviceAddress              global_index_buffer_address,
    VkPipelineLayout             pipeline_layout,
    const std::filesystem::path& ray_generation_shader,
    const std::filesystem::path& ray_miss_shader,
    const std::filesystem::path& ray_closest_hit_shader,
    const std::filesystem::path& ray_any_hit_shader
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

    std::vector<Mesh> unique_meshes;
    for (auto& m : meshes) {
        bool is_unique = true;
        for (auto& unique_mesh : unique_meshes) {
            if (is_mesh_same(m, unique_mesh)) {
                is_unique = false;
                break;
            }
        }

        if (is_unique) {
            unique_meshes.push_back(m);
        }
    }

    std::vector<BLAS> bottom_level_acceleration_structures;

    for (auto& mesh : unique_meshes) {
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
    for (int i = 0; i < meshes.size(); i++) {
        const Mesh& mesh = meshes[i];

        uint32_t mesh_index = 0;
        uint32_t idx        = 0;
        for (const auto& unique_mesh : unique_meshes) {
            if (is_mesh_same(mesh, unique_mesh)) {
                break;
            }

            mesh_index++;
        }

        glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
        model           = glm::scale(model, glm::vec3(mesh.scale));

        VkAccelerationStructureInstanceKHR instance = {
            .transform                              = glm_to_vk_transform(model),
            .instanceCustomIndex                    = static_cast<uint32_t>(i),
            .mask                                   = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags                                  = 0,
            .accelerationStructureReference         = bottom_level_acceleration_structures[mesh_index].address
        };

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

    // Ray generation group
    {
        shader_stages.push_back(load_shader(ray_generation_shader, VK_SHADER_STAGE_RAYGEN_BIT_KHR));
        VkRayTracingShaderGroupCreateInfoKHR raygen_group_ci{};
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
        shader_stages.push_back(load_shader(ray_miss_shader, VK_SHADER_STAGE_MISS_BIT_KHR));
        VkRayTracingShaderGroupCreateInfoKHR miss_group_ci{};
        miss_group_ci.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        miss_group_ci.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        miss_group_ci.generalShader      = static_cast<uint32_t>(shader_stages.size()) - 1;
        miss_group_ci.closestHitShader   = VK_SHADER_UNUSED_KHR;
        miss_group_ci.anyHitShader       = VK_SHADER_UNUSED_KHR;
        miss_group_ci.intersectionShader = VK_SHADER_UNUSED_KHR;
        shader_groups.push_back(miss_group_ci);
    }

    // Ray closest hit group
    {
        shader_stages.push_back(load_shader(ray_closest_hit_shader, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
        VkRayTracingShaderGroupCreateInfoKHR closes_hit_group_ci{};
        closes_hit_group_ci.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        closes_hit_group_ci.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        closes_hit_group_ci.generalShader      = VK_SHADER_UNUSED_KHR;
        closes_hit_group_ci.closestHitShader   = static_cast<uint32_t>(shader_stages.size()) - 1;
        closes_hit_group_ci.anyHitShader       = VK_SHADER_UNUSED_KHR;
        closes_hit_group_ci.intersectionShader = VK_SHADER_UNUSED_KHR;

        if (!ray_any_hit_shader.empty()) {
            shader_stages.push_back(load_shader(ray_any_hit_shader, VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
            closes_hit_group_ci.anyHitShader = static_cast<uint32_t>(shader_stages.size()) - 1;
        }

        shader_groups.push_back(closes_hit_group_ci);
    }

    VkRayTracingPipelineCreateInfoKHR raytracing_pipeline_create_info{};
    raytracing_pipeline_create_info.sType      = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    raytracing_pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
    raytracing_pipeline_create_info.pStages    = shader_stages.data();
    raytracing_pipeline_create_info.groupCount = static_cast<uint32_t>(shader_groups.size());
    raytracing_pipeline_create_info.pGroups    = shader_groups.data();
    raytracing_pipeline_create_info.maxPipelineRayRecursionDepth = 3;
    raytracing_pipeline_create_info.layout                       = pipeline_layout;

    VkPipeline rt_pipeline;
    VK_CHECK(vkCreateRayTracingPipelinesKHR(
        device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &raytracing_pipeline_create_info, nullptr, &rt_pipeline
    ));

    const uint32_t handle_size = ray_tracing_properties.shaderGroupHandleSize;
    const uint32_t handle_size_aligned =
        aligned_size(ray_tracing_properties.shaderGroupHandleSize, ray_tracing_properties.shaderGroupHandleAlignment);
    const uint32_t           handle_alignment       = ray_tracing_properties.shaderGroupHandleAlignment;
    const uint32_t           group_count            = static_cast<uint32_t>(shader_groups.size());
    const uint32_t           sbt_size               = group_count * handle_size_aligned;
    const VkBufferUsageFlags sbt_buffer_usage_flags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const VmaAllocationCreateFlagBits sbt_memory_usage = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    Buffer raygen_shader_binding_table = create_buffer(
        handle_size,
        sbt_buffer_usage_flags,
        allocator,
        sbt_memory_usage,
        ray_tracing_properties.shaderGroupBaseAlignment
    );
    Buffer miss_shader_binding_table = create_buffer(
        handle_size,
        sbt_buffer_usage_flags,
        allocator,
        sbt_memory_usage,
        ray_tracing_properties.shaderGroupBaseAlignment
    );
    Buffer hit_shader_binding_table = create_buffer(
        handle_size,
        sbt_buffer_usage_flags,
        allocator,
        sbt_memory_usage,
        ray_tracing_properties.shaderGroupBaseAlignment
    );

    std::vector<uint8_t> shader_handle_storage(sbt_size);
    VK_CHECK(vkGetRayTracingShaderGroupHandlesKHR(
        device, rt_pipeline, 0, group_count, sbt_size, shader_handle_storage.data()
    ));

    copy_to_buffer(raygen_shader_binding_table, allocator, shader_handle_storage.data(), handle_size);
    copy_to_buffer(
        miss_shader_binding_table, allocator, shader_handle_storage.data() + handle_size_aligned, handle_size
    );
    copy_to_buffer(
        hit_shader_binding_table, allocator, shader_handle_storage.data() + handle_size_aligned * 2, handle_size
    );

    VkStridedDeviceAddressRegionKHR raygen_shader_sbt_entry{};
    raygen_shader_sbt_entry.deviceAddress = get_buffer_device_address(raygen_shader_binding_table, device);
    raygen_shader_sbt_entry.stride        = handle_size_aligned;
    raygen_shader_sbt_entry.size          = handle_size_aligned;

    VkStridedDeviceAddressRegionKHR miss_shader_sbt_entry{};
    miss_shader_sbt_entry.deviceAddress = get_buffer_device_address(miss_shader_binding_table, device);
    miss_shader_sbt_entry.stride        = handle_size_aligned;
    miss_shader_sbt_entry.size          = handle_size_aligned;

    VkStridedDeviceAddressRegionKHR hit_shader_sbt_entry{};
    hit_shader_sbt_entry.deviceAddress = get_buffer_device_address(hit_shader_binding_table, device);
    hit_shader_sbt_entry.stride        = handle_size_aligned;
    hit_shader_sbt_entry.size          = handle_size_aligned;

    for (auto& stage : shader_stages) {
        vkDestroyShaderModule(device, stage.module, nullptr);
    }

    RTScene scene = {
        .blas_instances                  = bottom_level_acceleration_structures,
        .tlas                            = top_level_acceleration_structure,
        .rt_properties                   = ray_tracing_properties,
        .acceleration_structure_features = acceleration_structure_features,
        .pipeline                        = rt_pipeline,
        .raygen_shader_binding_table     = raygen_shader_binding_table,
        .miss_shader_binding_table       = miss_shader_binding_table,
        .hit_shader_binding_table        = hit_shader_binding_table,
        .raygen_shader_sbt_entry         = raygen_shader_sbt_entry,
        .miss_shader_sbt_entry           = miss_shader_sbt_entry,
        .hit_shader_sbt_entry            = hit_shader_sbt_entry
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

    vkDestroyPipeline(device, scene.pipeline, nullptr);
    destroy_buffer(scene.raygen_shader_binding_table, device, allocator);
    destroy_buffer(scene.hit_shader_binding_table, device, allocator);
    destroy_buffer(scene.miss_shader_binding_table, device, allocator);
}
