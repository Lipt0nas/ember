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

    // TODO: Dunno about this one
    Buffer instance_buffer;
    Buffer scratch_buffer;
};

bool is_mesh_same(const Mesh& m1, const Mesh& m2);

VkTransformMatrixKHR glm_to_vk_transform(const glm::mat4& mat);

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
);

// NOTE: does not handle updates to BLAS, and is probably horribly inneficient
void rebuild_tlas(
    const RTScene&                   scene,
    VkDevice                         device,
    VmaAllocator                     allocator,
    VkCommandBuffer                  command_buffer,
    const std::vector<Mesh>&         meshes,
    const std::vector<MeshInstance>& mesh_instances
);

void destroy_rt_scene(const RTScene& scene, VkDevice device, VmaAllocator allocator);
