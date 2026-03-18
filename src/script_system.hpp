#pragma once

#include "components.hpp"
#include "ember.hpp"
#include "scene.hpp"

struct ScriptPropertyDescription {
    enum class Type {
        UNKNOWN,
        BOOL,
        INT,
        FLOAT,
        STRING,
        ENUM,
        VEC2,
        VEC3,
        VEC4,
        QUAT,
    };

    Type           type;
    ScriptProperty default_value;

    std::string name;
    int         index;
    int         type_id;
    int         offset;

    // NOTE: would be nice to move this out to ScriptSystem instead
    std::vector<std::string> enum_values;
};

struct Script {
    bool valid;

    std::string name;
    std::string source;

    class asIScriptModule* module;

    class asIScriptFunction* constructor;
    class asIScriptFunction* on_start;
    class asIScriptFunction* on_update;
    class asIScriptFunction* on_fixed_update;

    std::vector<ScriptPropertyDescription> editable_properties;
};

class ScriptSystem {
public:
    ScriptSystem();

    void initialize(class World* world);

    void load_scripts();
    void reload_scripts();

    void generate_predefined_file();
    // Destroys all script objects from nodes that have the script component
    void clear();

    const std::unordered_map<uint32_t, Script>& get_scripts();

    void construct_script_objects(Entity entity, components::Script& script);
    void call_on_start(const components::Script& script);
    void call_on_update(const components::Script& script, float delta);
    void call_on_fixed_update(const components::Script& script, float delta);

    friend void node_get_component(class asIScriptGeneric* gen);

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

    class asIScriptEngine* engine         = nullptr;
    class CScriptBuilder*  script_builder = nullptr;
    class World*           world          = nullptr;

    // NOTE: is there a need for more?
    class asIScriptContext* context = nullptr;

    std::unordered_map<uint32_t, Script> scripts;

    void                delete_node(Entity node, bool delete_children);
    Entity              find_node(const std::string& name);
    class CScriptArray* find_nodes_with_tag(const std::string& tag);

    bool cast_ray(glm::vec3 origin, glm::vec3 dir, float max_distance, float& t, uint32_t& entity);

    int      node_id_property_index = -1;
    uint32_t get_node_id_from_object(class asIScriptObject* object);

    std::string prelude_code;

    std::unordered_map<int, std::function<void*(Scene& scene, Entity e)>> component_retrieve_map;

    void register_node_type(class asIScriptEngine* engine);
    void register_camera_component(class asIScriptEngine* engine);
    void register_character_controller_component(class asIScriptEngine* engine);
    void register_sound_component(class asIScriptEngine* engine);
    void register_light_component(class asIScriptEngine* engine);
};
