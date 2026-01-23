#include "script_system.hpp"
#include "glm/gtc/random.hpp"

#include <fstream>
#include <iterator>

#include <angelscript.h>
#include <scriptarray.h>
#include <scriptbuilder.h>
#include <scriptdictionary.h>
#include <scriptgrid.h>
#include <scripthandle.h>
#include <scriptstdstring.h>
#include <weakref.h>

namespace {
    void script_message_callback(const asSMessageInfo* msg, void* param) {
        switch (msg->type) {
        case asMSGTYPE_ERROR:
            spdlog::error("{} ({}:{}): {}", msg->section, msg->row, msg->col, msg->message);
            break;
        case asMSGTYPE_WARNING:
            spdlog::warn("{} ({}:{}): {}", msg->section, msg->row, msg->col, msg->message);
            break;
        case asMSGTYPE_INFORMATION:
            spdlog::info("{} ({}:{}): {}", msg->section, msg->row, msg->col, msg->message);
            break;
        }
    }

    void script_log_trace(const std::string& string) {
        spdlog::trace("{}", string);
    }

    void script_log_debug(const std::string& string) {
        spdlog::debug("{}", string);
    }

    void script_log_info(const std::string& string) {
        spdlog::info("{}", string);
    }

    void script_log_warn(const std::string& string) {
        spdlog::warn("{}", string);
    }

    void script_log_error(const std::string& string) {
        spdlog::error("{}", string);
    }

    void script_log_critical(const std::string& string) {
        spdlog::critical("{}", string);
    }

    float script_rand_float(float min, float max) {
        return glm::linearRand(min, max);
    }

    int script_rand_int(int min, int max) {
        return glm::linearRand(min, max);
    }
} // namespace

ScriptSystem::ScriptSystem(Scene& scene, JPH::PhysicsSystem& physics_system)
    : scene(scene), physics_system(physics_system) {
    engine = asCreateScriptEngine();

    engine->SetMessageCallback(asFUNCTION(script_message_callback), 0, asCALL_CDECL);
    auto default_namespace = engine->GetDefaultNamespace();

    RegisterScriptArray(engine, true);
    RegisterStdString(engine);
    RegisterStdStringUtils(engine);
    RegisterScriptHandle(engine);
    RegisterScriptWeakRef(engine);
    RegisterScriptDictionary(engine);
    RegisterScriptGrid(engine);

    engine->RegisterInterface("INode");

    engine->SetDefaultNamespace("Log");
    engine->RegisterGlobalFunction("void trace(string &in)", asFUNCTION(script_log_trace), asCALL_CDECL);
    engine->RegisterGlobalFunction("void debug(string &in)", asFUNCTION(script_log_debug), asCALL_CDECL);
    engine->RegisterGlobalFunction("void info(string &in)", asFUNCTION(script_log_info), asCALL_CDECL);
    engine->RegisterGlobalFunction("void warn(string &in)", asFUNCTION(script_log_warn), asCALL_CDECL);
    engine->RegisterGlobalFunction("void error(string &in)", asFUNCTION(script_log_error), asCALL_CDECL);
    engine->RegisterGlobalFunction("void critical(string &in)", asFUNCTION(script_log_critical), asCALL_CDECL);

    engine->SetDefaultNamespace("World");
    engine->RegisterGlobalFunction(
        "void clone_node(string &in, float, float, float)",
        asMETHOD(ScriptSystem, ScriptSystem::clone_node),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->SetDefaultNamespace("Random");
    engine->RegisterGlobalFunction("float random_float(float, float)", asFUNCTION(script_rand_float), asCALL_CDECL);
    engine->RegisterGlobalFunction("int random_int(int, int)", asFUNCTION(script_rand_int), asCALL_CDECL);

    engine->SetDefaultNamespace(default_namespace);

    context = engine->CreateContext();
}

void ScriptSystem::load_scripts(const std::filesystem::path& path) {
    auto hash_script = [](const std::filesystem::path& path) -> uint32_t {
        uint32_t hash = 0;

        for (auto& it : path.string()) {
            hash = 37 * hash + 17 + static_cast<char>(it);
        }

        return hash;
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        if (entry.path().extension() != ".as") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) {
            continue;
        }

        std::string source(std::istreambuf_iterator<char>(file), {});

        asIScriptModule* script_module =
            engine->GetModule(entry.path().filename().string().c_str(), asGM_ALWAYS_CREATE);

        int r;
        r = script_module->AddScriptSection(entry.path().filename().string().c_str(), source.c_str(), source.length());
        if (r < 0) {
            spdlog::error("Failed to load script {}", entry.path().string());
            continue;
        }

        r = script_module->Build();
        if (r < 0) {
            spdlog::error("Failed to build script {}", entry.path().string());
            continue;
        }

        asITypeInfo* type       = nullptr;
        int          type_count = script_module->GetObjectTypeCount();
        for (int i = 0; i < type_count; i++) {
            bool found_type     = false;
            type                = script_module->GetObjectTypeByIndex(i);
            int interface_count = type->GetInterfaceCount();
            for (int j = 0; j < interface_count; j++) {
                if (strcmp(type->GetInterface(j)->GetName(), "INode") == 0) {
                    found_type = true;
                    break;
                }
            }

            if (found_type == true) {
                break;
            }

            type = nullptr;
        }

        if (!type) {
            spdlog::warn("Script doesn't implement INode, it will not be loaded");
            continue;
        }

        std::string constructor_name = std::string(type->GetName()) + "@ " + std::string(type->GetName()) + "()";
        auto        constructor      = type->GetFactoryByDecl(constructor_name.c_str());
        if (!constructor) {
            spdlog::warn("Script doesn't have a default constructor, it will not be loaded");
            continue;
        }

        auto on_update       = type->GetMethodByDecl("void update(float)");
        auto on_fixed_update = type->GetMethodByDecl("void fixed_update(float)");

        auto hash = hash_script(entry.path().filename());

        scripts.insert(
            {hash,
             Script{
                 .name            = entry.path().filename().string(),
                 .source          = source,
                 .constructor     = constructor,
                 .on_update       = on_update,
                 .on_fixed_update = on_fixed_update,
             }}
        );
    }
}

const std::unordered_map<uint32_t, Script>& ScriptSystem::get_scripts() {
    return scripts;
}

void ScriptSystem::initialize(components::Script& s) {
    if (scripts.contains(s.script_id)) {
        auto& script = scripts.at(s.script_id);
        if (script.constructor && !s.object) {
            context->Prepare(script.constructor);
            int r = context->Execute();
            if (r != asEXECUTION_FINISHED && r == asEXECUTION_EXCEPTION) {
                spdlog::error(
                    "Exception executing script: {} ({}:{})",
                    context->GetExceptionString(),
                    context->GetExceptionFunction()->GetDeclaration(),
                    context->GetExceptionLineNumber()
                );
            } else if (r == asEXECUTION_FINISHED) {
                s.object = *((asIScriptObject**)context->GetAddressOfReturnValue());
                ((asIScriptObject*)s.object)->AddRef();
            }
            context->Unprepare();
        }
    }
}

void ScriptSystem::call_on_update(const components::Script& s, float delta) {
    if (scripts.contains(s.script_id)) {
        auto& script = scripts.at(s.script_id);
        if (script.on_update && s.object) {
            context->Prepare(script.on_update);
            context->SetObject(s.object);
            context->SetArgFloat(0, delta);
            int r = context->Execute();
            if (r != asEXECUTION_FINISHED && r == asEXECUTION_EXCEPTION) {
                spdlog::error(
                    "Exception executing script: {} ({}:{})",
                    context->GetExceptionString(),
                    context->GetExceptionFunction()->GetDeclaration(),
                    context->GetExceptionLineNumber()
                );
            }
            context->Unprepare();
        }
    }
}

void ScriptSystem::call_on_fixed_update(const components::Script& s, float delta) {
    if (scripts.contains(s.script_id)) {
        auto& script = scripts.at(s.script_id);
        if (script.on_fixed_update && s.object) {
            context->Prepare(script.on_fixed_update);
            context->SetObject(s.object);
            context->SetArgFloat(0, delta);
            int r = context->Execute();
            if (r != asEXECUTION_FINISHED && r == asEXECUTION_EXCEPTION) {
                spdlog::error(
                    "Exception executing script: {} ({}:{})",
                    context->GetExceptionString(),
                    context->GetExceptionFunction()->GetDeclaration(),
                    context->GetExceptionLineNumber()
                );
            }
            context->Unprepare();
        }
    }
}

void ScriptSystem::clone_node(const std::string& name, float x, float y, float z) {
    auto view = scene.entity_registry.view<components::Name>();
    for (auto [e, n] : view.each()) {
        if (n.name == name) {
            clone_node_internal(e, x, y, z);
            break;
        }
    }
}

Entity ScriptSystem::clone_node_internal(Entity e, float x, float y, float z) {
    auto src_name = scene.get_component<components::Name>(e);
    spdlog::info("cloning internal {}", src_name->name);

    Entity new_entity                                                = scene.create_entity(src_name->name + "_clone");
    scene.get_component<components::Transform>(new_entity)->position = glm::vec3(x, y, z);

    auto src_physics = scene.get_component<components::Physics>(e);
    if (src_physics) {
        auto& p              = scene.add_component<components::Physics>(new_entity);
        auto& body_interface = physics_system.GetBodyInterface();

        JPH::EMotionType motion_type = body_interface.GetMotionType(src_physics->body_id);
        JPH::Vec3        position    = body_interface.GetPosition(src_physics->body_id);
        JPH::Quat        rotation    = body_interface.GetRotation(src_physics->body_id);

        const JPH::Shape* shape = body_interface.GetShape(src_physics->body_id);

        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(position),
            rotation,
            motion_type,
            motion_type == JPH::EMotionType::Static ? Layers::NON_MOVING : Layers::MOVING
        );

        settings.mFriction      = body_interface.GetFriction(src_physics->body_id);
        settings.mGravityFactor = body_interface.GetGravityFactor(src_physics->body_id);

        JPH::Body*  new_body = body_interface.CreateBody(settings);
        JPH::BodyID new_id   = new_body->GetID();

        body_interface.AddBody(new_id, JPH::EActivation::Activate);

        p.body_id   = new_id;
        p.is_static = motion_type == JPH::EMotionType::Static;
    }

    auto src_mesh = scene.get_component<components::Mesh>(e);
    if (src_mesh) {
        auto& m = scene.add_component<components::Mesh>(new_entity);
        m.mesh  = src_mesh->mesh;
    }

    auto src_parent = scene.get_component<components::Parent>(e);
    if (src_parent) {
        scene.set_node_parent(new_entity, src_parent->parent);
    }

    auto src_children = scene.get_component<components::Children>(e);
    if (src_children) {
        for (Entity child : src_children->children) {
            Entity new_child = clone_node_internal(child, x, y, z);
            scene.set_node_parent(new_child, new_entity);
        }
    }

    return new_entity;
}
