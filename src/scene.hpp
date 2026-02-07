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

    Entity create_node(const std::string& name = "New Node");
    void   delete_node(Entity node, bool delete_children = false);
    Entity clone_node(Entity base);

    bool                node_has_tag(Entity e, const std::string tag);
    std::vector<Entity> find_nodes_with_tag(const std::string tag);

    void set_node_parent(Entity child, Entity parent);
    void remove_node_parent(Entity child);

    template <typename T, typename... Args> T& add_component(Entity entity, Args&&... args) {
        return add_component_intenal<T>(entity, args...);
    }

    template <typename T> void remove_component(Entity entity) {
        if (entity_registry.all_of<T>(entity)) {
            remove_component_internal<T>(entity);
        }
    }

    template <typename T> T* get_component(Entity entity) {
        return entity_registry.try_get<T>(entity);
    }

    template <typename T> Entity get_node_from_component(T& component) {
        return entt::to_entity(entity_registry.storage<T>(), component);
    }

private:
    class World* world = nullptr;

    Entity clone_node_internal(Entity base, Entity cloned_parent);
    void   remove_all_components(Entity node);

    template <typename T> void remove_component_internal(Entity node) {
        entity_registry.remove<T>(node);
    }

    template <typename T, typename... Args> T& add_component_intenal(Entity entity, Args&&... args) {
        return entity_registry.emplace<T>(entity, std::forward<Args>(args)...);
    }
};

template <> void Scene::remove_component_internal<components::Physics>(Entity entity);
