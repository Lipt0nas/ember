#pragma once

#include "components.hpp"
#include "ember.hpp"
#include "geometry.hpp"
#include "resources.hpp"

#include <filesystem>

using Entity = entt::entity;

class Scene {
public:
    entt::registry entity_registry;

    Scene();

    void initialize(class World* world);
    void cleanup();

    Entity create_node(const std::string& name = "New Node");
    void   delete_node(Entity node, bool delete_children = false);
    Entity clone_node(Entity base);

    bool                node_has_tag(Entity e, const std::string tag);
    std::vector<Entity> find_nodes_with_tag(const std::string tag);

    void set_node_parent(Entity child, Entity parent, bool keep_local = false);
    void remove_node_parent(Entity child);

    template <typename T, typename... Args> T& add_component(Entity entity, Args&&... args) {
        return add_component_intenal<T>(entity, args...);
    }

    template <typename T> void remove_component(Entity entity) {
        if (entity_registry.all_of<T>(entity)) {
            remove_component_internal<T>(entity);
        }
    }

    template <typename T> T* get_component(Entity entity) {
        return entity_registry.try_get<T>(entity);
    }

    template <typename T> Entity get_node_from_component(T& component) {
        return entt::to_entity(entity_registry.storage<T>(), component);
    }

    template <typename T> Entity find_component() {
        auto view = entity_registry.view<T>();
        return view.front();
    }

private:
    class World* world = nullptr;

    Entity clone_node_internal(Entity base, Entity cloned_parent);
    void   remove_all_components(Entity node);

    template <typename T> void remove_component_internal(Entity node) {
        entity_registry.remove<T>(node);
    }

    template <typename T, typename... Args> T& add_component_intenal(Entity entity, Args&&... args) {
        return entity_registry.emplace<T>(entity, std::forward<Args>(args)...);
    }

    void on_mesh_component_added(entt::registry& registry, entt::entity e);
};

template <> void Scene::remove_component_internal<components::Physics>(Entity entity);
