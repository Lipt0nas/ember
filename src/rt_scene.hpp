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

bool is_mesh_same(const Mesh& m1, const Mesh& m2);

VkTransformMatrixKHR glm_to_vk_transform(const glm::mat4& mat);

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
);

void destroy_rt_scene(const RTScene& scene, VkDevice device, VmaAllocator allocator);
