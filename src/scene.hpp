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

struct Scene {
    std::vector<Mesh>          meshes;
    std::vector<ImageResource> images;
    std::vector<Sampler>       samplers;
    std::vector<Material>      materials;

    entt::registry entity_registry;
};

Entity scene_create_entity(Scene& scene, const std::string& name = "");

template <typename T, typename... Args> T& scene_add_component(Scene& scene, Entity entity, Args&&... args) {
    return scene.entity_registry.emplace<T>(entity, std::forward<Args>(args)...);
}

template <typename T> T* scene_get_component(Scene& scene, Entity entity) {
    return scene.entity_registry.try_get<T>(entity);
}

void load_scene(
    Scene&                       scene,
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

void destroy_scene(const Scene& scene, VkDevice device, VmaAllocator allocator);
