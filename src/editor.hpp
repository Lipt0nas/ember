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
    Editor(std::unordered_map<uint32_t, VkDescriptorSet>& imgui_material_image_handles);

    void initialize(World* world);

    void render_main_menu(std::function<void(std::string)> scene_save_callback);
    void render_scene_hierarchy_window();
    void render_scene_node_property_window();
    void render_performance_window(const std::vector<std::pair<std::string, PassTiming>>& passes);

    void   set_selected_entity(Entity e);
    Entity get_selected_entity();

    template <typename T> void render_component_ui(Entity e) {
        ImGui::Text("Unimplemented");
    }

private:
    void draw_node_in_hierarchy(Entity e, Entity& selected_entity);

    bool draw_vec3_controls(
        const std::string& label,
        glm::vec3&         value,
        float              reset_to     = 0.0f,
        bool               uniform      = false,
        float              column_width = 70.0f
    );

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
