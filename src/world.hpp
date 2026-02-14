#pragma once

#include "ember.hpp"

#include "asset_registry.hpp"
#include "input_system.hpp"
#include "physics.hpp"
#include "renderer.hpp"
#include "resources.hpp"
#include "scene.hpp"
#include "script_system.hpp"

class World {
public:
    Renderer      renderer;
    PhysicsSystem physics;
    InputSystem   input;
    ScriptSystem  script;
    Scene         scene;
    AssetRegistry asset_registry;

    struct {
        std::vector<Mesh>                   meshes;
        std::vector<ImageResource>          images;
        std::unordered_map<size_t, Sampler> samplers;
        std::vector<Material>               materials;
        std::vector<Material>               runtime_materials;
    } resources;

    // True when in the "play" state
    bool is_running = false;

    bool needs_blas_rebuild = false;

    World();

    void initialize(struct SDL_Window* window, bool meshlets_enabled, bool hardware_rt_enabled, bool vsync);

    int load_texture(AssetID id);
    int load_texture(const std::string& path);

    int load_mesh(AssetID id);
    int load_mesh(const std::string& path);

    int       load_material(AssetID id);
    int       load_material(const std::string& path);
    Material* get_material(AssetID id);

    int  dedicate_material(components::Material& mat, int original_id);
    void apply_material_override(components::Material& mat);

    std::string load_script(AssetID id);
    std::string load_script(const std::string& path);

    bool load_collision_mesh(AssetID id, JPH::TriangleList& triangles);

    std::unordered_map<AssetID, int> texture_map;
    std::unordered_map<AssetID, int> mesh_map;
    std::unordered_map<AssetID, int> material_map;

    void cleanup();

private:
    void    register_bindless_texture(int index, const Sampler& sampler);
    Sampler get_sampler(const SamplerDescription& description);
};
