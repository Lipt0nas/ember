#pragma once

#include "components.hpp"
#include "ember.hpp"
#include "scene.hpp"

struct Script {
    bool valid;

    std::string name;
    std::string source;

    class asIScriptModule* module;

    class asIScriptFunction* constructor;
    class asIScriptFunction* on_update;
    class asIScriptFunction* on_fixed_update;
};

class ScriptSystem {
public:
    ScriptSystem();

    void initialize(class World* world);

    void load_scripts(const std::filesystem::path& path);
    void reload_scripts();

    void generate_predefined_file();
    // Destroys all script objects from nodes that have the script component
    void clear();

    const std::unordered_map<uint32_t, Script>& get_scripts();

    void initialize(components::Script& script);
    void call_on_update(const components::Script& script, float delta);
    void call_on_fixed_update(const components::Script& script, float delta);

    void set_player_position(glm::vec3 position) {
        this->player_pos = position;
    }

    void set_player_look_dir(glm::vec3 direction) {
        this->player_look_dir = direction;
    }

    void set_player_velocity(glm::vec3 velocity) {
        this->player_velocity = velocity;
    }

private:
    std::filesystem::path script_source_dir;

    class asIScriptEngine* engine         = nullptr;
    class CScriptBuilder*  script_builder = nullptr;
    class World*           world          = nullptr;

    // NOTE: is there a need for more?
    class asIScriptContext* context = nullptr;

    std::unordered_map<uint32_t, Script> scripts;

    Entity clone_node(const std::string& name);
    Entity get_node(const std::string& name);

    bool cast_ray(glm::vec3 origin, glm::vec3 dir, float max_distance, float& t, uint32_t& entity);

    void set_node_position(Entity entity, glm::vec3 position);
    void set_node_scale(Entity entity, float scale);

    glm::vec3   get_node_position(Entity entity);
    float       get_node_scale(Entity entity);
    std::string get_node_name(Entity entity);

    void set_node_physics_body_box(Entity entity, glm::vec3 half_extents);
    void set_node_physics_linear_velocity(Entity entity, glm::vec3 velocity);
    void set_node_physics_angular_velocity(Entity entity, glm::vec3 velocity);
    void disable_node_physics(Entity entity);
    void enable_node_physics(Entity entity);

    void node_dedicate_material(Entity entity);
    void node_set_material_emissive(Entity entity, glm::vec3 emissive);

    glm::vec3 player_pos;
    glm::vec3 get_player_position() {
        return player_pos;
    }

    glm::vec3 player_look_dir;
    glm::vec3 get_player_look_direction() {
        return player_look_dir;
    }

    glm::vec3 player_velocity;
    glm::vec3 get_player_velocity() {
        return player_velocity;
    }
};
