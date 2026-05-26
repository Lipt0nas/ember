#pragma once

#include "components.hpp"
#include "ember.hpp"
#include "input_system.hpp"
#include "physics.hpp"
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

    class asIScriptFunction* on_collision_started;
    class asIScriptFunction* on_collision_ended;

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
    void call_on_collision_started(const components::Script& script, const CollisionStarted& e);
    void call_on_collision_ended(const components::Script& script, const CollisionEnded& e);

    friend void node_get_component(class asIScriptGeneric* gen);
    friend void node_add_component(class asIScriptGeneric* gen);

    friend void bind_event(class asIScriptGeneric* gen);

    template <typename T> void issue_event(const T& event) {
        Event e;
        e.type_id = event_id<T>();
        e.data.resize(sizeof(T));
        memcpy(e.data.data(), &event, sizeof(T));

        engine_event_queue.push_back(std::move(e));
    }

    void flush_events();

private:
    struct Event {
        int                  type_id;
        std::vector<uint8_t> data;
    };

    struct EngineEventSubscription {
        class asIScriptFunction* handler;
    };
    std::unordered_map<int, std::vector<EngineEventSubscription>> engine_event_subscriptions;

    std::mutex         event_queue_mutex;
    std::vector<Event> engine_event_queue;

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
    bool cast_ray_hit_point(
        glm::vec3 origin, glm::vec3 dir, float max_distance, glm::vec3& out_pos, glm::vec3& out_normal, uint32_t& entity
    );

    int      node_id_property_index = -1;
    uint32_t get_node_id_from_object(class asIScriptObject* object);

    std::string prelude_code;

    std::unordered_map<int, std::function<void*(Scene& scene, Entity e)>> component_retrieve_map;
    std::unordered_map<int, std::function<void*(Scene& scene, Entity e)>> component_create_map;

    void register_node_type(class asIScriptEngine* engine);
    void register_components(class asIScriptEngine* engine);
    void register_engine_events(class asIScriptEngine* engine);

    template <typename T> int event_id();
};

template <> int ScriptSystem::event_id<KeyDownEvent>();
template <> int ScriptSystem::event_id<KeyDownEvent>();
template <> int ScriptSystem::event_id<ContactAddedEvent>();
template <> int ScriptSystem::event_id<ContactRemovedEvent>();
