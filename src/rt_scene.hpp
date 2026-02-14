#pragma once

#include "ember.hpp"
#include "geometry.hpp"
#include "pipeline.hpp"
#include "resources.hpp"

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

    std::vector<Buffer> instance_buffers;
    std::vector<Buffer> scratch_buffers;

    uint32_t max_tlas_instance_count;
};

bool is_mesh_same(const Mesh& m1, const Mesh& m2);

VkTransformMatrixKHR glm_to_vk_transform(const glm::mat4& mat);

RTScene create_rt_scene(
    VkDevice         device,
    VkPhysicalDevice physical_device,
    VmaAllocator     allocator,
    VkCommandBuffer  command_buffer,
    VkQueue          queue,
    uint32_t         max_tlas_instance_count,
    uint32_t         frames_in_flight
);

void rebuild_blas(
    RTScene&        scene,
    class World*    world,
    uint32_t        frame_index,
    VkCommandBuffer command_buffer,
    VkDeviceAddress global_vertex_buffer_address,
    VkDeviceAddress global_index_buffer_address
);

void rebuild_tlas(
    RTScene&                         scene,
    VkDevice                         device,
    VmaAllocator                     allocator,
    VkCommandBuffer                  command_buffer,
    uint32_t                         frame_index,
    const std::vector<Mesh>&         meshes,
    const std::vector<MeshInstance>& mesh_instances
);

void destroy_rt_scene(const RTScene& scene, VkDevice device, VmaAllocator allocator);
