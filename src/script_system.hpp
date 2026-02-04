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
    class asIScriptFunction* on_start;
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

    void construct_script_objects(Entity entity, components::Script& script);
    void call_on_start(const components::Script& script);
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

    friend void node_get_component(class asIScriptGeneric* gen);
    friend void node_physics_set_linear_velocity(glm::vec3 velocity, components::Physics* p);
    friend void node_physics_set_angular_velocity(glm::vec3 velocity, components::Physics* p);
    friend void node_physics_set_friction(float friction, components::Physics* p);
    friend void node_physics_set_restitution(float restitution, components::Physics* p);
    friend void node_physics_set_active(bool active, components::Physics* p);
    friend void node_physics_set_box_body(glm::vec3 half_extents, float mass, components::Physics* p);

    friend void      node_mesh_material_make_dedicated(components::Mesh* m);
    friend Material* node_mesh_get_material(components::Mesh* m);
    friend Mesh*     node_mesh_get_mesh(components::Mesh* m);

private:
    struct EventSubscription {
        uint32_t                 node;
        class asIScriptFunction* callback;
    };
    std::unordered_map<int, std::vector<EventSubscription>> event_subscriptions;

    void     subscribe_to_event(Entity entity, int event_type, class asIScriptFunction* callback);
    void     unsubscribe_from_event(Entity, int event_type);
    void     publish_event(class asIScriptObject* msg);
    void     publish_event_to_node(Entity target, class asIScriptObject* msg);
    void     publish_event_to_tag(const std::string& tag, class asIScriptObject* msg);
    uint32_t get_event_type_from_message(class asIScriptObject* object);
    void     invoke_event_callback(const EventSubscription& sub, class asIScriptObject* msg);

    std::filesystem::path script_source_dir;

    class asIScriptEngine* engine         = nullptr;
    class CScriptBuilder*  script_builder = nullptr;
    class World*           world          = nullptr;

    // NOTE: is there a need for more?
    class asIScriptContext* context = nullptr;

    std::unordered_map<uint32_t, Script> scripts;

    Entity clone_node(const std::string& name);
    Entity get_node(const std::string& name);

    class CScriptArray* get_nodes_with_tag(const std::string& tag);

    bool cast_ray(glm::vec3 origin, glm::vec3 dir, float max_distance, float& t, uint32_t& entity);

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

    int      node_id_property_index = -1;
    uint32_t get_node_id_from_object(class asIScriptObject* object);

    std::string prelude_code;

    std::unordered_map<int, std::function<void*(Scene& scene, Entity e)>> component_retrieve_map;
};
