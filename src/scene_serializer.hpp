#pragma once

#include "ember.hpp"
#include "physics.hpp"
#include "scene.hpp"
#include "script_system.hpp"

struct GeometryDataHeader {
    uint64_t vertex_buffer_size;
    uint64_t index_buffer_size;
    uint64_t meshlet_buffer_size;
    uint64_t meshlet_vertex_indicies_buffer_size;
    uint64_t meshlet_primitive_buffer_size;
    uint64_t meshlet_bounds_buffer_size;
};

struct TextureDataHeader {
    uint32_t texture_count;
    bool     is_compressed;
    uint64_t compressed_data_size;
};

struct TextureHeader {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t mip_levels;
    int      sampler_index;
};

struct MipLevelHeader {
    uint32_t width;
    uint32_t height;
    uint32_t size;
};

struct SamplerInfo {
    uint32_t mag_filter;
    uint32_t min_filter;

    uint32_t mipmap_mode;

    uint32_t address_mode_u;
    uint32_t address_mode_v;
    uint32_t address_mode_w;

    float anisotropy;
};

class SceneSerializer {
public:
    static void load(
        const std::filesystem::path& path,
        Scene&                       scene,
        JPH::PhysicsSystem&          physics_system,
        ScriptSystem&                script_system,
        RendererBuffers&             buffers,
        BufferOffsets&               buffer_offsets,
        std::vector<unsigned char>&  compressed_texture_data,
        VkDevice                     device,
        VkQueue                      queue,
        VmaAllocator                 allocator,
        VkCommandBuffer              command_buffer
    );

    static void save(
        const std::filesystem::path&      path,
        const Scene&                      scene,
        const JPH::PhysicsSystem&         physics_system,
        const ScriptSystem&               script_system,
        const RendererBuffers&            buffers,
        const BufferOffsets&              buffer_offsets,
        const std::vector<unsigned char>& compressed_texture_data,
        VkDevice                          device,
        VkQueue                           queue,
        VmaAllocator                      allocator,
        VkCommandBuffer                   command_buffer
    );
};
