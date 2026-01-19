#pragma once

#include "components.hpp"
#include "ember.hpp"
#include "geometry.hpp"
#include "physics.hpp"
#include "resources.hpp"

#include <filesystem>

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
    std::vector<Material>      materials;

    entt::registry entity_registry;

    void load_scene(
        const std::filesystem::path& path,
        const Buffer&                staging_buffer,
        const Buffer&                vertex_buffer,
        const Buffer&                index_buffer,
        const Buffer&                meshlet_buffer,
        const Buffer&                meshlet_vertex_indices,
        const Buffer&                meshlet_primitive_buffer,
        const Buffer&                meshlet_bounds_buffer,
        JPH::PhysicsSystem*          physics_system,
        bool                         build_lods,
        bool                         fast_build,
        VkDevice                     device,
        VkQueue                      queue,
        VmaAllocator                 allocator,
        VkCommandBuffer              command_buffer
    );

    void destroy_scene(VkDevice device, VmaAllocator allocator);

    Entity create_entity(const std::string& name = "");
    void   set_node_parent(Entity child, Entity parent);
    void   remove_node_parent(Entity child);

    template <typename T, typename... Args> T& add_component(Entity entity, Args&&... args) {
        return entity_registry.emplace<T>(entity, std::forward<Args>(args)...);
    }

    template <typename T> T* get_component(Entity entity) {
        return entity_registry.try_get<T>(entity);
    }
};
