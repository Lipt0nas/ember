#pragma once

#include "components.hpp"
#include "ember.hpp"
#include "geometry.hpp"
#include "resources.hpp"

#include <filesystem>

// TODO: Not an ideal location
struct RendererBuffers {
    Buffer staging_buffer;
    Buffer vertex_buffer;
    Buffer index_buffer;
    Buffer meshlet_buffer;
    Buffer meshlet_vertex_indices;
    Buffer meshlet_primitive_buffer;
    Buffer meshlet_bounds_buffer;
};

// TODO: This should probably be merged into the buffer struct somehow, or wrapped in another struct
struct BufferOffsets {
    uint64_t vertex_buffer;
    uint64_t index_buffer;
    uint64_t meshlet_buffer;
    uint64_t meshlet_vertex_indices;
    uint64_t meshlet_primitive_buffer;
    uint64_t meshlet_bounds_buffer;
};

struct ImageResource {
    Image image;
    int   sampler_index;
};

using Entity = entt::entity;

class Scene {
public:
    std::vector<Mesh>          meshes;
    std::vector<ImageResource> images;
    std::vector<Sampler>       samplers;

    std::vector<Material> materials;
    // How many materials were loaded on scene load, used when rolling back from the gameplay state
    size_t original_material_size;

    entt::registry entity_registry;

    void initialize(class World* world);

    void load_scene(
        const std::filesystem::path& path,
        RendererBuffers&             buffers,
        BufferOffsets&               buffer_offsets,
        bool                         build_lods,
        bool                         fast_build,
        bool                         compress_textures,
        std::vector<unsigned char>&  compressed_texture_data,
        VkDevice                     device,
        VkQueue                      queue,
        VmaAllocator                 allocator,
        VkCommandBuffer              command_buffer
    );

    void destroy_scene(VkDevice device, VmaAllocator allocator);

    Entity create_node(const std::string& name = "");
    Entity clone_node(Entity base);

    void set_node_parent(Entity child, Entity parent);
    void remove_node_parent(Entity child);

    template <typename T, typename... Args> T& add_component(Entity entity, Args&&... args) {
        return entity_registry.emplace<T>(entity, std::forward<Args>(args)...);
    }

    template <typename T> void remove_component(Entity entity) {
        spdlog::info("Removing component");
        entity_registry.remove<T>(entity);
    }

    template <typename T> T* get_component(Entity entity) {
        return entity_registry.try_get<T>(entity);
    }

private:
    class World* world = nullptr;
};

template <> void Scene::remove_component<components::Physics>(Entity entity);
