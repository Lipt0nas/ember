#pragma once

#include "ember.hpp"

#include "editor.hpp"
#include "scene.hpp"
#include "world.hpp"

#include <functional>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>

class ComponentRegistry {
public:
    using SaveFunc = std::function<void(World&, Entity, cereal::JSONOutputArchive&)>;
    using LoadFunc = std::function<void(World&, Entity, cereal::JSONInputArchive&)>;

    using SnapshotSaveFunc = std::function<void(entt::snapshot& snapshot, cereal::BinaryOutputArchive&)>;
    using SnapshotLoadFunc = std::function<void(entt::snapshot_loader& loader, cereal::BinaryInputArchive&)>;

    using EditorRenderUIFunc = std::function<bool(Entity)>;

    using HasComponentFunc    = std::function<bool(World&, Entity)>;
    using AddComponentFunc    = std::function<void(World&, Entity)>;
    using RemoveComponentFunc = std::function<void(World&, Entity)>;

    struct ComponentDescription {
        bool removable             = true;
        bool show_in_editor        = true;
        bool accessible_in_scripts = true;
        bool save_to_disk          = true;
        bool save_snapshot         = true;
    };

    struct ComponentInfo {
        std::string          name;
        ComponentDescription description;

        SaveFunc save_func;
        LoadFunc load_func;

        SnapshotSaveFunc save_snapshot;
        SnapshotLoadFunc load_snapshot;

        EditorRenderUIFunc render_editor_ui;

        HasComponentFunc    has_component;
        AddComponentFunc    add_component;
        RemoveComponentFunc remove_component;
    };

    static void register_components(Editor* editor);

    template <typename T>
    static void register_component(Editor* editor, const std::string& name, const ComponentDescription& description) {
        spdlog::debug("Registering component: {}", name);

        ComponentInfo info;
        info.name        = name;
        info.description = description;

        info.has_component = [](World& world, auto e) {
            return world.scene.get_component<T>(e) != nullptr;
        };

        info.add_component = [](World& world, auto e) {
            world.scene.add_component<T>(e);
        };

        info.remove_component = [](World& world, auto e) {
            world.scene.remove_component<T>(e);
        };

        if (description.save_to_disk) {
            info.save_func = [name](World& world, auto e, auto& archive) {
                if (world.scene.get_component<T>(e)) {
                    archive(cereal::make_nvp(name, *world.scene.get_component<T>(e)));
                }
            };

            info.load_func = [name](World& world, auto e, auto& archive) {
                T component;
                archive(component);

                world.scene.add_component<T>(e, component);
            };
        }

        if (description.save_snapshot) {
            info.save_snapshot = [](entt::snapshot& snapshot, auto& archive) {
                snapshot.get<T>(archive);
            };

            info.load_snapshot = [](entt::snapshot_loader& loader, auto& archive) {
                loader.get<T>(archive);
            };
        }

        if (editor && description.show_in_editor) {
            info.render_editor_ui = [editor](auto e) {
                return editor->render_component_ui<T>(e);
            };
        }

        components_by_name[name]                        = info;
        components_by_type[entt::type_hash<T>::value()] = info;
    }

    static void save_node(World& world, Entity e, cereal::JSONOutputArchive& archive);
    static void load_node(World& world, Entity e, cereal::JSONInputArchive& archive);

    static void save_snapshot(entt::snapshot& snapphot, cereal::BinaryOutputArchive& archive);
    static void load_snapshot(entt::snapshot_loader& loader, cereal::BinaryInputArchive& archive);

    static const ComponentInfo* get_by_name(const std::string& name);

    static const auto& get_all() {
        return components_by_name;
    }

private:
    static inline std::map<std::string, ComponentInfo>   components_by_name;
    static inline std::map<entt::id_type, ComponentInfo> components_by_type;
};
