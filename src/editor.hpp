#pragma once

#include "ember.hpp"
#include "framegraph.hpp"
#include "scene.hpp"
#include "script_system.hpp"
#include "world.hpp"

struct SceneNodeComponentInfo {
    std::string name;

    bool removable;

    std::function<void(Entity)> render_ui;
    std::function<void(Entity)> add_component;
    std::function<void(Entity)> remove_component;
};

class Editor {
public:
    Editor(World* world, std::unordered_map<uint32_t, VkDescriptorSet>& imgui_material_image_handles);

    template <typename T> void register_component(const std::string& name, bool is_removable) {
        auto index = entt::type_hash<T>::value();
        node_component_map.insert_or_assign(
            index,
            SceneNodeComponentInfo{
                .name      = name,
                .removable = is_removable,
                .render_ui =
                    [this](Entity e) {
                        render_component_ui<T>(e);
                    },
                .add_component =
                    [this](Entity e) {
                        add_component<T>(e);
                    },
                .remove_component =
                    [this](Entity e) {
                        remove_component<T>(e);
                    },
            }
        );
    }

    void render_main_menu(std::function<void(std::string)> scene_save_callback);
    void render_scene_hierarchy_window();
    void render_scene_node_property_window();
    void render_performance_window(const std::vector<std::pair<std::string, PassTiming>>& passes);

    void   set_selected_entity(Entity e);
    Entity get_selected_entity();

private:
    void draw_node_in_hierarchy(Entity e, Entity& selected_entity);
    bool entity_has_component(entt::id_type component_type, Entity entity);

    template <typename T> void add_component(Entity e) {
        world->scene.add_component<T>(e);
    }

    template <typename T> void remove_component(Entity e) {
        world->scene.remove_component<T>(e);
    }

    template <typename T> void render_component_ui(Entity e) {
        ImGui::Text("Unimplemented");
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
    World* world           = nullptr;

    std::unordered_map<uint32_t, VkDescriptorSet>& imgui_material_image_handles;
};

template <> void Editor::render_component_ui<components::Transform>(Entity e);
template <> void Editor::render_component_ui<components::Mesh>(Entity e);
template <> void Editor::render_component_ui<components::Script>(Entity e);
template <> void Editor::render_component_ui<components::Name>(Entity e);
template <> void Editor::render_component_ui<components::Physics>(Entity e);
template <> void Editor::render_component_ui<components::Tag>(Entity e);
