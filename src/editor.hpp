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

    bool render_main_menu(std::function<void(std::string)> scene_save_callback);
    bool render_scene_hierarchy_window();
    bool render_scene_node_property_window();
    bool render_performance_window(const std::vector<std::pair<std::string, PassTiming>>& passes);

    void   set_selected_entity(Entity e);
    Entity get_selected_entity();

    template <typename T> bool render_component_ui(Entity e) {
        ImGui::Text("Unimplemented");
        return false;
    }

private:
    void draw_node_in_hierarchy(Entity e, Entity& selected_entity, bool& change);

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

template <> bool Editor::render_component_ui<components::Transform>(Entity e);
template <> bool Editor::render_component_ui<components::Mesh>(Entity e);
template <> bool Editor::render_component_ui<components::Script>(Entity e);
template <> bool Editor::render_component_ui<components::Name>(Entity e);
template <> bool Editor::render_component_ui<components::Physics>(Entity e);
template <> bool Editor::render_component_ui<components::Tag>(Entity e);
template <> bool Editor::render_component_ui<components::Camera>(Entity e);
template <> bool Editor::render_component_ui<components::CharacterController>(Entity e);
