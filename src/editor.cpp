#include "editor.hpp"

#include "component_registry.hpp"
#include "embedded.hpp"
#include "imgui_internal.h"

#include <imgui.h>
#include <imgui_stdlib.h>

template <> bool Editor::render_component_ui<components::Transform>(Entity e) {
    bool edited = false;

    auto* t = world->scene.get_component<components::Transform>(e);
    ImGui::SeparatorText("Local Transform");
    edited |= draw_vec3_controls("Position", t->position);

    glm::vec3 temp_scale = glm::vec3(t->scale);
    if (draw_vec3_controls("Scale", temp_scale, 1.0f, true)) {
        if (temp_scale.x < 0.0f) {
            temp_scale.x = 0.0f;
        }

        t->scale = temp_scale.x;
        edited |= true;
    }

    // NOTE: this doesn't work, might need to store rotation as euler angles
    glm::vec3 temp_rotation = glm::vec3(t->rotation.x, t->rotation.y, t->rotation.z);
    if (draw_vec3_controls("Rotation", temp_rotation)) {
        t->rotation =
            glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(temp_rotation.x), glm::vec3(1, 0, 0)) * t->rotation;
        t->rotation =
            glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(temp_rotation.y), glm::vec3(0, 1, 0)) * t->rotation;
        t->rotation =
            glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(temp_rotation.z), glm::vec3(0, 0, 1)) * t->rotation;
        edited = true;
    }

    ImGui::SeparatorText("World Transform");
    glm::vec3 temp_position = t->world_position;
    draw_vec3_controls("World Position", temp_position);

    temp_scale = glm::vec3(t->world_scale);
    draw_vec3_controls("World Scale", temp_scale, 1.0f, true);

    temp_rotation = glm::vec3(t->world_rotation.x, t->world_rotation.y, t->world_rotation.z);
    draw_vec3_controls("World Rotation", temp_rotation);

    return edited;
}

template <> bool Editor::render_component_ui<components::Mesh>(Entity e) {
    bool edited = false;

    auto* m = world->scene.get_component<components::Mesh>(e);
    if (ImGui::TreeNode("Material")) {
        auto& material = world->scene.materials[m->mesh.material_id];

        std::vector<uint32_t*> material_indices = {
            &material.albedo_index, &material.normals_index, &material.material_index, &material.emissive_index
        };

        for (auto id : material_indices) {
            if (*id != 0) {
                ImGui::Image(imgui_material_image_handles.at(*id), ImVec2(50, 50));
            } else {
                ImGui::Text("Empty");
            }
            if (ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("texture_id");
                if (payload) {
                    uint32_t new_id = *(uint32_t*)payload->Data;
                    if (new_id != *id) {
                        *id    = new_id;
                        edited = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
        }

        ImGui::NewLine();

        ImGui::SliderFloat("Roughness Factor", &material.roughness_factor, 0.0, 1.0f);
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::SliderFloat("Metallic Factor", &material.metallic_factor, 0.0, 1.0f);
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::SliderFloat("Normal Scale", &material.normal_scale, 0.0, 1.0f);
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::ColorEdit4("Albedo Factor", &material.albedo_factor.x);
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::ColorEdit3(
            "Emissive Factor", &material.emissive_factor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR
        );
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Mesh##prop")) {
        ImGui::Text("Unimplemented");
        ImGui::TreePop();
    }

    return edited;
}

namespace {
    bool render_bool_property(const std::string& name, ScriptProperty& prop) {
        bool value = std::get<bool>(prop.value);
        if (ImGui::Checkbox(name.c_str(), &value)) {
            prop.value = value;
            return true;
        }
        return false;
    }

    bool render_int_property(const std::string& name, ScriptProperty& prop) {
        int value = std::get<int>(prop.value);
        if (ImGui::DragInt(name.c_str(), &value)) {
            prop.value = value;
            return true;
        }
        return false;
    }

    bool
    render_enum_property(const std::string& name, ScriptProperty& prop, const std::vector<std::string>& enum_values) {
        int current_value = std::get<int>(prop.value);

        std::string current_name = "Unknown";
        for (int i = 0; i < enum_values.size(); i++) {
            if (current_value == i) {
                current_name = enum_values[i];
            }
        }

        bool changed = false;
        if (ImGui::BeginCombo(name.c_str(), current_name.c_str())) {
            for (int i = 0; i < enum_values.size(); i++) {
                bool is_selected = (i == current_value);
                if (ImGui::Selectable(enum_values[i].c_str(), is_selected)) {
                    prop.value = i;
                    changed    = true;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        return changed;
    }

    bool render_float_property(const std::string& name, ScriptProperty& prop) {
        float value = std::get<float>(prop.value);
        if (ImGui::DragFloat(name.c_str(), &value)) {
            prop.value = value;
            return true;
        }
        return false;
    }

    bool render_vec2_property(const std::string& name, ScriptProperty& prop) {
        glm::vec2 value = std::get<glm::vec2>(prop.value);
        if (ImGui::DragFloat2(name.c_str(), &value.x)) {
            prop.value = value;
            return true;
        }
        return false;
    }

    bool render_vec3_property(const std::string& name, ScriptProperty& prop) {
        glm::vec3 value = std::get<glm::vec3>(prop.value);
        if (ImGui::DragFloat3(name.c_str(), &value.x)) {
            prop.value = value;
            return true;
        }
        return false;
    }

    bool render_vec4_property(const std::string& name, ScriptProperty& prop) {
        glm::vec4 value = std::get<glm::vec4>(prop.value);
        if (ImGui::DragFloat4(name.c_str(), &value.x)) {
            prop.value = value;
            return true;
        }
        return false;
    }

    bool render_quat_property(const std::string& name, ScriptProperty& prop) {
        glm::quat value = std::get<glm::quat>(prop.value);
        if (ImGui::DragFloat4(name.c_str(), &value.x)) {
            prop.value = value;
            return true;
        }
        return false;
    }

    bool render_string_property(const std::string& name, ScriptProperty& prop) {
        std::string& value = std::get<std::string>(prop.value);

        static std::string temp_string;
        if (temp_string.compare(value) != 0) {
            temp_string = value;
        }

        if (ImGui::InputText(name.c_str(), &temp_string, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (value.compare(temp_string) != 0) {
                value = temp_string;
                return true;
            }
        }
        return false;
    }
} // namespace

template <> bool Editor::render_component_ui<components::Script>(Entity e) {
    bool edited = false;

    auto* s       = world->scene.get_component<components::Script>(e);
    auto  scripts = world->script.get_scripts();

    int script_to_remove = -1;
    for (int i = 0; i < s->scripts.size(); i++) {
        ScriptInstance& instance = s->scripts[i];

        Script* script = nullptr;
        if (scripts.contains(instance.script_id)) {
            script = &scripts.at(instance.script_id);
        }

        if (script) {
            ImGui::SeparatorText(script->name.c_str());

            for (auto& prop : script->editable_properties) {
                ScriptProperty current_prop;
                bool           overriden = false;

                if (auto it = instance.property_overrides.find(prop.name); it != instance.property_overrides.end()) {
                    current_prop = it->second;
                    overriden    = true;
                } else {
                    current_prop = prop.default_value;
                }

                bool changed = false;
                switch (prop.type) {
                case ScriptPropertyDescription::Type::BOOL:
                    changed = render_bool_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::INT:
                    changed = render_int_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::FLOAT:
                    changed = render_float_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::STRING:
                    changed = render_string_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::ENUM:
                    changed = render_enum_property(prop.name, current_prop, prop.enum_values);
                    break;
                case ScriptPropertyDescription::Type::VEC2:
                    changed = render_vec2_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::VEC3:
                    changed = render_vec3_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::VEC4:
                    changed = render_vec4_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::QUAT:
                    changed = render_quat_property(prop.name, current_prop);
                    break;
                case ScriptPropertyDescription::Type::UNKNOWN:
                    break;
                }

                if (changed) {
                    instance.property_overrides[prop.name] = current_prop;
                    edited                                 = true;
                }

                if (overriden) {
                    ImGui::SameLine();
                    ImGui::PushID(prop.name.c_str());
                    if (ImGui::Button(ICON_FA_REDO)) {
                        instance.property_overrides.erase(prop.name);
                        edited = true;
                    }
                    ImGui::PopID();
                }
            }
        }

        ImGui::PushID(instance.script_id);
        if (ImGui::Button("Remove Script")) {
            script_to_remove = i;
        }
        ImGui::PopID();
    }

    if (script_to_remove != -1) {
        s->scripts.erase(s->scripts.begin() + script_to_remove);
        edited = true;
    }

    ImGui::NewLine();
    ImGui::Separator();
    static uint32_t id_to_add = 0;
    Script*         script    = nullptr;
    if (scripts.contains(id_to_add)) {
        script = &scripts.at(id_to_add);
    }
    if (ImGui::BeginCombo("Script Source", (script ? script->name.c_str() : "None"))) {
        for (auto& [id, script] : scripts) {
            // Currently scripts that don't have node behavior are distinguished by having no constructor
            if (!script.constructor || !script.valid) {
                continue;
            }

            if (ImGui::Selectable(script.name.c_str())) {
                id_to_add = id;
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Add Script") && scripts.contains(id_to_add)) {
        s->scripts.push_back({.script_id = static_cast<uint32_t>(id_to_add)});
        id_to_add = 0;
        edited    = true;
    }

    return edited;
}

template <> bool Editor::render_component_ui<components::Physics>(Entity e) {
    auto* p = world->scene.get_component<components::Physics>(e);

    if (p->body_id.IsInvalid()) {
        ImGui::Text("Invalid");
    } else {
        ImGui::Text("BodyID: %u", p->body_id.GetIndex());
        ImGui::Text("Static : %u", p->is_static);
    }

    return false;
}

template <> bool Editor::render_component_ui<components::Tag>(Entity e) {
    bool edited = false;

    auto* t = world->scene.get_component<components::Tag>(e);
    auto* n = world->scene.get_component<components::Name>(e);

    std::string tag_to_remove = "";
    for (size_t i = 0; i < t->tags.size(); i++) {
        std::string id  = std::string(n->name + "##tag") + std::to_string(i);
        auto&       tag = t->tags[i];

        ImGui::PushID(id.c_str());
        static std::string temp_string;
        if (temp_string.compare(tag) != 0) {
            temp_string = tag;
        }

        if (ImGui::InputText("", &temp_string, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (tag.compare(temp_string) != 0) {
                tag    = temp_string;
                edited = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
            tag_to_remove = tag;
        }
        ImGui::PopID();
    }

    if (!tag_to_remove.empty()) {
        std::erase(t->tags, tag_to_remove);
        edited = true;
    }

    if (ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
        t->tags.push_back("NewTag");
        edited = true;
    }

    return edited;
}

template <> bool Editor::render_component_ui<components::Name>(Entity e) {
    bool edited = false;

    auto* n = world->scene.get_component<components::Name>(e);

    static std::string temp_string;
    if (temp_string.compare(n->name) != 0) {
        temp_string = n->name;
    }

    if (ImGui::InputText("##name", &temp_string, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (n->name.compare(temp_string) != 0) {
            n->name = temp_string;
            edited  = true;
        }
    }

    return edited;
}

template <> bool Editor::render_component_ui<components::Camera>(Entity e) {
    bool edited = false;

    auto* c = world->scene.get_component<components::Camera>(e);

    ImGui::InputFloat("Near Plane", &c->near_plane);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::InputFloat("Far Plane", &c->far_plane);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("Viewport X", &c->viewport_x, 0.0f, 1.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("Viewport Y", &c->viewport_y, 0.0f, 1.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("Viewport Width", &c->viewport_width, 0.0f, 1.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("Viewport Height", &c->viewport_height, 0.0f, 1.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("FOV", &c->fov, 1.0f, 180.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::InputFloat("Ortho Size", &c->ortho_size);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    if (ImGui::Checkbox("Active", &c->is_active)) {
        edited = true;
    }

    return edited;
}

template <> bool Editor::render_component_ui<components::CharacterController>(Entity e) {
    bool edited = false;

    auto* c = world->scene.get_component<components::CharacterController>(e);

    ImGui::DragFloat("Height", &c->height, 0.1f, 0.1f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat("Radius", &c->radius, 0.1f, 0.1f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat("Step Down Distance", &c->step_down_distance, 0.1f, 0.01f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat("Step Up Height", &c->step_up_height, 0.1f, 0.01f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat("Max Slow Angle", &c->max_slope_angle, 0.1f, 0.01f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    if (ImGui::Checkbox("Enhanced Edge Removal", &c->enhanced_edge_removal)) {
        edited = true;
    }

    return edited;
}

Editor::Editor(std::unordered_map<uint32_t, VkDescriptorSet>& imgui_material_image_handles)
    : imgui_material_image_handles(imgui_material_image_handles) {
}

void Editor::initialize(World* world) {
    this->world = world;
}

bool Editor::render_main_menu(std::function<void(std::string)> scene_save_callback) {
    static char save_scene_path[256] = "\0";
    static bool show_save_popup      = false;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save scene")) {
                show_save_popup = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Editor")) {
            if (ImGui::MenuItem("Reload scripts")) {
                world->script.reload_scripts();
            }
            if (ImGui::MenuItem("Generate predefined script file")) {
                world->script.generate_predefined_file();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (show_save_popup) {
        ImGui::OpenPopup("SaveScenePopup");
        show_save_popup = false;
    }

    if (ImGui::BeginPopupModal("SaveScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save Scene");
        ImGui::Separator();

        ImGui::InputText("Path", save_scene_path, 256);

        if (ImGui::Button("Save")) {
            scene_save_callback(save_scene_path);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    return false;
}

bool Editor::render_scene_hierarchy_window() {
    bool change = false;

    ImGui::Begin(ICON_FA_SITEMAP " Scene Hierarchy");
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Create Node")) {
            world->scene.create_node();
            change = true;
        }
        ImGui::EndPopup();
    }

    auto view =
        world->scene.entity_registry.view<components::Transform, components::Name>(entt::exclude<components::Parent>);
    for (auto [e, t, n] : view.each()) {
        draw_node_in_hierarchy(e, selected_entity, change);
    }

    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
        selected_entity = entt::null;
    }
    ImGui::End();

    return change;
}

void Editor::set_selected_entity(Entity e) {
    this->selected_entity = e;
}

Entity Editor::get_selected_entity() {
    return this->selected_entity;
}

void Editor::draw_node_in_hierarchy(Entity e, Entity& selected_entity, bool& change) {
    int delete_mode = 0;

    bool has_children = false;
    auto children     = world->scene.get_component<components::Children>(e);
    if (children) {
        if (!children->children.empty()) {
            has_children = true;
        }
    }

    auto name = world->scene.get_component<components::Name>(e);

    ImGuiTreeNodeFlags flags =
        ((selected_entity == e) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
    flags |= ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (!has_children) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    ImGui::PushID((uint64_t)e);
    bool opened = ImGui::TreeNodeEx(&e, flags, "%s", name->name.c_str());
    ImGui::PopID();

    if (ImGui::BeginDragDropSource(
            ImGuiDragDropFlags_SourceNoHoldToOpenOthers | ImGuiDragDropFlags_SourceNoDisableHover
        )) {
        ImGui::SetDragDropPayload("hierarchy_node", &e, sizeof(Entity));
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("hierarchy_node");
        if (payload) {
            Entity new_child = *(Entity*)payload->Data;
            world->scene.set_node_parent(new_child, e);
            change |= true;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Add New Node")) {
            auto node = world->scene.create_node();
            world->scene.set_node_parent(node, e);
            change |= true;
        }

        ImGui::Separator();
        if (ImGui::BeginMenu("Add Component")) {
            for (auto [name, info] : ComponentRegistry::get_all()) {
                if (!info.description.show_in_editor) {
                    continue;
                }

                if (!info.has_component(*world, e)) {
                    if (ImGui::MenuItem(name.c_str())) {
                        info.add_component(*world, e);
                        change |= true;
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Remove Component")) {
            for (auto [name, info] : ComponentRegistry::get_all()) {
                if (!info.description.show_in_editor) {
                    continue;
                }

                if (info.has_component(*world, e) && info.description.removable) {
                    if (ImGui::MenuItem(name.c_str())) {
                        info.remove_component(*world, e);
                        change |= true;
                    }
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::BeginMenu("Delete")) {
            if (ImGui::MenuItem("This Node Only")) {
                delete_mode = 1;
            }
            if (ImGui::MenuItem("This Node And Children")) {
                delete_mode = 2;
            }
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    if (ImGui::IsItemClicked()) {
        selected_entity = e;
    }

    if (opened) {
        if (children) {
            for (auto& child : children->children) {
                draw_node_in_hierarchy(child, selected_entity, change);
            }
        }

        ImGui::TreePop();
    }

    if (delete_mode != 0) {
        if (selected_entity == e) {
            selected_entity = entt::null;
        }

        world->scene.delete_node(e, delete_mode == 2);
        change |= true;
    }
}

bool Editor::render_scene_node_property_window() {
    bool change = false;
    ImGui::Begin(ICON_FA_WRENCH " Node Properties");
    if (selected_entity != entt::null) {
        for (auto [name, info] : ComponentRegistry::get_all()) {
            if (!info.description.show_in_editor) {
                continue;
            }

            if (info.has_component(*world, selected_entity)) {
                ImGui::PushID(name.c_str());
                if (info.description.removable) {
                    if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
                        info.remove_component(*world, selected_entity);
                        change = true;
                        ImGui::PopID();
                        continue;
                    } else {
                        ImGui::SameLine();
                    }
                }

                if (ImGui::CollapsingHeader(info.name.c_str())) {
                    ImGui::PushID("Widget");
                    change |= info.render_editor_ui(selected_entity);
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();

    return change;
}

bool Editor::render_performance_window(const std::vector<std::pair<std::string, PassTiming>>& passes) {
    ImGui::Begin(ICON_FA_CLOCK " Performance");
    if (ImGui::BeginTable("PassStats", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Pass");
        ImGui::TableSetupColumn("Avg (ms)");
        ImGui::TableSetupColumn("% of Frame");
        ImGui::TableHeadersRow();

        float              total_avg = 0.0f;
        std::vector<float> avg_timings;

        for (const auto& [name, timing] : passes) {
            float avg = timing.get_avg_timing_ms();

            avg_timings.push_back(avg);
            total_avg += avg;
        }

        for (const auto& [name, timing] : passes) {
            float avg        = timing.get_avg_timing_ms();
            float percentage = (avg / total_avg) * 100.0f;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            ImVec4 color = ImVec4(1, 1, 1, 1);
            if (percentage > 40.0f)
                color = ImVec4(1, 0.3f, 0.3f, 1);
            else if (percentage > 20.0f)
                color = ImVec4(1, 1, 0, 1);
            else
                color = ImVec4(0.3f, 1, 0.3f, 1);

            ImGui::TextColored(color, "%s", name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", avg);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f%%", percentage);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "TOTAL");
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.3f", total_avg);
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.2f GPU Time FPS", 1000.0f / total_avg);
        ImGui::EndTable();

        ImGui::SeparatorText("Percentage:");
        for (size_t i = 0; i < passes.size(); i++) {
            float percentage = (avg_timings[i] / total_avg) * 100.0f;
            ImGui::Text("%s", passes[i].first.c_str());
            ImGui::SameLine(200);
            ImGui::ProgressBar(
                avg_timings[i] / total_avg,
                ImVec2(-1, 0),
                (std::format("{}: {:.1f}%", passes[i].first, percentage)).c_str()
            );
        }
    }
    ImGui::End();

    return false;
}

bool Editor::draw_vec3_controls(
    const std::string& label, glm::vec3& value, float reset_to, bool uniform, float column_width
) {
    bool edited = false;

    ImGui::PushID(label.c_str());

    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, column_width);
    ImGui::Text("%s", label.c_str());
    ImGui::NextColumn();

    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0, 0});

    float  line_height = ImGui::GetFontBaked()->Size + GImGui->Style.FramePadding.y * 2.0f;
    ImVec2 button_size = {line_height + 3.0f, line_height};

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.9f, 0.2f, 0.2f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.8f, 0.1f, 0.15f, 1.0f});
    if (ImGui::Button("X", button_size)) {
        edited = true;

        value.x = reset_to;

        if (uniform) {
            value.y = reset_to;
            value.z = reset_to;
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::DragFloat("##X", &value.x, 0.1f, 0.0f, 0.0f, "%.2f")) {
        if (uniform) {
            value.y = value.x;
            value.z = value.x;
        }
    }
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.3f, 0.8f, 0.3f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.2f, 0.7f, 0.2f, 1.0f});
    if (ImGui::Button("Y", button_size)) {
        edited = true;

        value.y = reset_to;

        if (uniform) {
            value.x = reset_to;
            value.z = reset_to;
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::DragFloat("##Y", &value.y, 0.1f, 0.0f, 0.0f, "%.2f")) {
        if (uniform) {
            value.x = value.y;
            value.z = value.y;
        }
    }
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.35f, 0.9f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    if (ImGui::Button("Z", button_size)) {
        value.z = reset_to;

        if (uniform) {
            value.y = reset_to;
            value.x = reset_to;
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::DragFloat("##Z", &value.z, 0.1f, 0.0f, 0.0f, "%.2f")) {
        if (uniform) {
            value.x = value.z;
            value.y = value.z;
        }
    }
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::PopItemWidth();
    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();

    return edited;
}
