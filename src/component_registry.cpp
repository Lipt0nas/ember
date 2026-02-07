#include "component_registry.hpp"

void ComponentRegistry::register_components(Editor* editor) {
    ComponentRegistry::register_component<components::Transform>(
        editor,
        "Transform",
        {
            .removable             = false,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Name>(
        editor,
        "Name",
        {
            .removable             = false,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Parent>(
        editor,
        "Parent",
        {
            .removable             = true,
            .show_in_editor        = false,
            .accessible_in_scripts = false,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Children>(
        editor,
        "Children",
        {
            .removable             = true,
            .show_in_editor        = false,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Mesh>(
        editor,
        "Mesh",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Script>(
        editor,
        "Script",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = false,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Tag>(
        editor,
        "Tag",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Physics>(
        editor,
        "Physics",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = false,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Camera>(
        editor,
        "Camera",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::CharacterController>(
        editor,
        "CharacterController",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );
}

void ComponentRegistry::save_node(World& world, Entity e, cereal::JSONOutputArchive& archive) {
    for (auto& [name, info] : components_by_name) {
        if (info.description.save_to_disk) {
            info.save_func(world, e, archive);
        }
    }
}

void ComponentRegistry::load_node(World& world, Entity e, cereal::JSONInputArchive& archive) {
    for (auto& [name, info] : components_by_name) {
        if (info.description.save_to_disk) {
            try {
                archive.setNextName(name.c_str());
                info.load_func(world, e, archive);
            } catch (const cereal::Exception&) {

            } catch (const std::exception& ex) {
                spdlog::warn("Failed to load component: {}: {}", name, ex.what());
            }
        }
    }
}

void ComponentRegistry::save_snapshot(entt::snapshot& snapphot, cereal::BinaryOutputArchive& archive) {
    for (auto& [name, info] : components_by_name) {
        if (info.description.save_snapshot) {
            info.save_snapshot(snapphot, archive);
        }
    }
}

void ComponentRegistry::load_snapshot(entt::snapshot_loader& loader, cereal::BinaryInputArchive& archive) {
    for (auto& [name, info] : components_by_name) {
        if (info.description.save_snapshot) {
            info.load_snapshot(loader, archive);
        }
    }
}

const ComponentRegistry::ComponentInfo* ComponentRegistry::get_by_name(const std::string& name) {
    auto it = components_by_name.find(name);

    return it != components_by_name.end() ? &it->second : nullptr;
}
