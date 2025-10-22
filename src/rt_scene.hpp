#pragma once

#include "ember.hpp"
#include "pipeline.hpp"
#include "resources.hpp"

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

    glm::vec3 center = {};
    float     radius = 0.0f;
};

struct MeshInstance {
    int mesh_id;
    int material_id;

    glm::vec3 position = {};
    float     scale    = 1.0f;

    glm::quat rotation = {0.0f, 0.0f, 0.0f, 1.0f};
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

void destroy_rt_scene(const RTScene& scene, VkDevice device, VmaAllocator allocator);
