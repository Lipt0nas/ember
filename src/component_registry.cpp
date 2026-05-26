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
            .save_to_disk          = true,
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

    ComponentRegistry::register_component<components::Material>(
        editor,
        "Material",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Sprite>(
        editor,
        "Sprite",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Text>(
        editor,
        "Text",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Sound>(
        editor,
        "Sound",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::ParticleEffect>(
        editor,
        "Particle Effect",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::World>(
        editor,
        "World",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = false,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Light>(
        editor,
        "Light",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::SkeletalAnimation>(
        editor,
        "Skeletal Animation",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::DirectionalLight>(
        editor,
        "Directional Light",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::Sky>(
        editor,
        "Sky",
        {
            .removable             = true,
            .show_in_editor        = true,
            .accessible_in_scripts = true,
            .save_to_disk          = true,
            .save_snapshot         = true,
        }
    );

    ComponentRegistry::register_component<components::DDGIVolume>(
        editor,
        "DDGI Volume",
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
    std::vector<std::string> names;
    for (auto& [name, info] : components_by_name) {
        if (info.description.save_to_disk && info.has_component(world, e)) {
            names.push_back(name);
        }
    }

    archive(cereal::make_nvp("component_names", names));

    archive.setNextName("components");
    archive.startNode();
    for (const auto& name : names) {
        components_by_name.at(name).save_func(world, e, archive);
    }
    archive.finishNode();
}

void ComponentRegistry::load_node(World& world, Entity e, cereal::JSONInputArchive& archive) {
    std::vector<std::string> names;
    archive(cereal::make_nvp("component_names", names));

    archive.setNextName("components");
    archive.startNode();
    for (const auto& name : names) {
        auto* info = get_by_name(name);
        if (!info || !info->description.save_to_disk) {
            archive.setNextName(name.c_str());
            archive.startNode();
            archive.finishNode();
            continue;
        }
        archive.setNextName(name.c_str());
        try {
            info->load_func(world, e, archive);
        } catch (...) {
            spdlog::warn("Failed to load {} component", name);
        }
    }
    archive.finishNode();
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
