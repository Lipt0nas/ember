#pragma once

#include "ember.hpp"
#include "framegraph.hpp"
#include "scene.hpp"
#include "script_system.hpp"

struct SceneNodeComponentInfo {
    std::string name;
    bool        removable;

    std::function<void(Scene&, Entity)> render_ui;
    std::function<void(Scene&, Entity)> add_component;
    std::function<void(Scene&, Entity)> remove_component;
};

class Editor {
public:
    Editor();

    template <typename T>
    void register_component(
        const std::string& name, bool is_removable, std::function<void(Scene&, Entity)> render_ui = nullptr
    ) {
        auto index = entt::type_hash<T>::value();
        node_component_map.insert_or_assign(
            index,
            SceneNodeComponentInfo{
                .name      = name,
                .removable = is_removable,
                .render_ui = render_ui,
                .add_component =
                    [this](Scene& scene, Entity e) {
                        add_component<T>(scene, e);
                    },
                .remove_component =
                    [this](Scene& scene, Entity e) {
                        remove_component<T>(scene, e);
                    },
            }
        );
    }

    void render_scene_hierarchy_window(Scene& scene);
    void render_scene_node_property_window(
        Scene&                                               scene,
        ScriptSystem&                                        script_system,
        const std::unordered_map<uint32_t, VkDescriptorSet>& imgui_material_image_handles
    );
    void render_performance_window(const std::vector<std::pair<std::string, PassTiming>>& passes);

    void   set_selected_entity(Entity e);
    Entity get_selected_entity();

private:
    void draw_node_in_hierarchy(Scene& scene, Entity e, Entity& selected_entity);
    bool entity_has_component(Scene& scene, entt::id_type component_type, Entity entity);

    template <typename T> void add_component(Scene& scene, Entity e) {
        scene.entity_registry.template emplace<T>(e);
    }

    template <typename T> void remove_component(Scene& scene, Entity e) {
        scene.entity_registry.template remove<T>(e);
    }

    bool draw_vec3_controls(
        const std::string& label,
        glm::vec3&         value,
        float              reset_to     = 0.0f,
        bool               uniform      = false,
        float              column_width = 70.0f
    );

    std::unordered_map<entt::id_type, SceneNodeComponentInfo> node_component_map;

    Entity selected_entity = entt::null;
};
