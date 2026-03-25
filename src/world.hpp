#pragma once

#include "ember.hpp"

#include "asset_registry.hpp"
#include "input_system.hpp"
#include "physics.hpp"
#include "renderer.hpp"
#include "resources.hpp"
#include "scene.hpp"
#include "script_system.hpp"
#include "skeletal_animation.hpp"
#include "sound_system.hpp"

class World {
public:
    Renderer      renderer;
    PhysicsSystem physics;
    InputSystem   input;
    ScriptSystem  script;
    SoundSystem   sound;
    Scene         scene;
    AssetRegistry asset_registry;

    struct {
        std::vector<Mesh>                   meshes;
        std::vector<ImageResource>          images;
        std::unordered_map<size_t, Sampler> samplers;
        std::vector<Material>               materials;
        std::vector<Material>               runtime_materials;
        std::vector<Font>                   fonts;
        std::vector<Sound>                  sounds;
        std::vector<ParticleEffectAsset>    particle_effects;
        std::vector<Skeleton>               skeletons;
        std::vector<Animation>              animations;
    } resources;

    // True when in the "play" state
    bool  is_running = false;
    float time       = 0.0f;

    bool needs_blas_rebuild = false;

    World();

    void initialize(
        struct SDL_Window* window, bool meshlets_enabled, bool hardware_rt_enabled, bool vsync, bool hdr_requested
    );

    int load_texture(AssetID id);
    int load_texture(const std::string& path);

    int load_mesh(AssetID id);
    int load_mesh(const std::string& path);

    int load_font(AssetID id);
    int load_font(const std::string& path);

    int load_sound(AssetID id);
    int load_sound(const std::string& path);

    int load_skeleton(AssetID id);
    int load_skeleton(const std::string& path);

    int load_animation(AssetID id);
    int load_animation(const std::string& path);

    int       load_material(AssetID id);
    int       load_material(const std::string& path);
    Material* get_material(AssetID id);

    int  dedicate_material(components::Material& mat, int original_id);
    void apply_material_override(components::Material& mat);

    std::string load_script(AssetID id);
    std::string load_script(const std::string& path);

    bool load_collision_mesh(AssetID id, JPH::TriangleList& triangles);

    int  load_particle_effect(AssetID id);
    int  load_particle_effect(const std::string& path);
    void reload_particle_effect(AssetID id);

    int node_play_sound(Entity e);
    int play_sound(AssetID id, bool spatial = false);
    int play_sound(const std::string& path, bool spatial = false);

    std::unordered_map<AssetID, int> texture_map;
    std::unordered_map<AssetID, int> mesh_map;
    std::unordered_map<AssetID, int> material_map;
    std::unordered_map<AssetID, int> font_map;
    std::unordered_map<AssetID, int> sound_map;
    std::unordered_map<AssetID, int> particle_effect_map;
    std::unordered_map<AssetID, int> skeleton_map;
    std::unordered_map<AssetID, int> animation_map;

    void cleanup();

private:
    void    register_bindless_texture(int index, const Sampler& sampler);
    Sampler get_sampler(const SamplerDescription& description);
};
