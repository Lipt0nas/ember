#pragma once

#include "components.hpp"
#include "ember.hpp"
#include "physics.hpp"
#include "scene.hpp"

struct Script {
    std::string name;
    std::string source;

    class asIScriptFunction* constructor;
    class asIScriptFunction* on_update;
    class asIScriptFunction* on_fixed_update;
};

class ScriptSystem {
public:
    ScriptSystem(Scene& scene, JPH::PhysicsSystem& physics_system);

    void load_scripts(const std::filesystem::path& path);

    const std::unordered_map<uint32_t, Script>& get_scripts();

    void initialize(components::Script& script);
    void call_on_update(const components::Script& script, float delta);
    void call_on_fixed_update(const components::Script& script, float delta);

private:
    Scene&              scene;
    JPH::PhysicsSystem& physics_system;

    class asIScriptEngine* engine;

    // NOTE: is there a need for more?
    class asIScriptContext* context;

    std::unordered_map<uint32_t, Script> scripts;

    void   clone_node(const std::string& name, float x, float y, float z);
    Entity clone_node_internal(Entity e, float x, float y, float z);
};
