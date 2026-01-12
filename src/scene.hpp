#pragma once

#include "ember.hpp"
#include "geometry.hpp"
#include "physics.hpp"
#include "resources.hpp"

#include <filesystem>

struct ImageResource {
    Image image;
    int   sampler_index;
};

struct Scene {
    std::vector<Mesh>          meshes;
    std::vector<ImageResource> images;
    std::vector<Sampler>       samplers;
    std::vector<Material>      materials;

    std::vector<MeshInstance> instances;

    std::vector<PhysicsObject> static_bodies;
    std::vector<PhysicsObject> dynamic_bodies;
};

Scene load_scene(
    const std::filesystem::path& path,
    const Buffer&                staging_buffer,
    const Buffer&                vertex_buffer,
    const Buffer&                index_buffer,
    const Buffer&                meshlet_buffer,
    const Buffer&                meshlet_vertex_indices,
    const Buffer&                meshlet_primitive_buffer,
    const Buffer&                meshlet_bounds_buffer,
    JPH::PhysicsSystem*          physics_system,
    VkDevice                     device,
    VkQueue                      queue,
    VmaAllocator                 allocator,
    VkCommandBuffer              command_buffer
);

void destroy_scene(const Scene& scene, VkDevice device, VmaAllocator allocator);
