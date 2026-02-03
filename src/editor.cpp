#include "editor.hpp"

#include "embedded.hpp"
#include "imgui_internal.h"

#include <imgui.h>
#include <imgui_stdlib.h>

template <> void Editor::render_component_ui<components::Transform>(Entity e) {
    auto* t = world->scene.get_component<components::Transform>(e);
    ImGui::SeparatorText("Local Transform");
    draw_vec3_controls("Position", t->position);

    glm::vec3 temp_scale = glm::vec3(t->scale);
    if (draw_vec3_controls("Scale", temp_scale, 1.0f, true)) {
        if (temp_scale.x < 0.0f) {
            temp_scale.x = 0.0f;
        }

        t->scale = temp_scale.x;
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
    }

    ImGui::SeparatorText("World Transform");
    glm::vec3 temp_position = t->world_position;
    draw_vec3_controls("World Position", temp_position);

    temp_scale = glm::vec3(t->world_scale);
    draw_vec3_controls("World Scale", temp_scale, 1.0f, true);

    temp_rotation = glm::vec3(t->world_rotation.x, t->world_rotation.y, t->world_rotation.z);
    draw_vec3_controls("World Rotation", temp_rotation);
}

template <> void Editor::render_component_ui<components::Mesh>(Entity e) {
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
                    *id = *(uint32_t*)payload->Data;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
        }

        ImGui::NewLine();

        ImGui::SliderFloat("Roughness Factor", &material.roughness_factor, 0.0, 1.0f);
        ImGui::SliderFloat("Metallic Factor", &material.metallic_factor, 0.0, 1.0f);
        ImGui::SliderFloat("Normal Scale", &material.normal_scale, 0.0, 1.0f);

        ImGui::ColorEdit4("Albedo Factor", &material.albedo_factor.x);
        ImGui::ColorEdit3(
            "Emissive Factor", &material.emissive_factor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR
        );

        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Mesh##prop")) {
        ImGui::Text("Unimplemented");
        ImGui::TreePop();
    }
}

template <> void Editor::render_component_ui<components::Script>(Entity e) {
    auto* s       = world->scene.get_component<components::Script>(e);
    auto  scripts = world->script.get_scripts();

    std::string current_name = "None";
    if (scripts.contains(s->script_id)) {
        current_name = scripts.at(s->script_id).name;
    }

    if (ImGui::BeginCombo("Script Source", current_name.c_str())) {
        for (auto& [id, script] : scripts) {
            // Currently scripts that don't have node behavior are distinguished by having no constructor
            if (!script.constructor || !script.valid) {
                continue;
            }

            if (ImGui::Selectable(script.name.c_str(), id == s->script_id)) {
                s->script_id = id;
            }
        }
        ImGui::EndCombo();
    }
}

template <> void Editor::render_component_ui<components::Physics>(Entity e) {
    auto* p = world->scene.get_component<components::Physics>(e);

    if (p->body_id.IsInvalid()) {
        ImGui::Text("Invalid");
    } else {
        ImGui::Text("BodyID: %u", p->body_id.GetIndex());
        ImGui::Text("Static : %u", p->is_static);
    }
}

template <> void Editor::render_component_ui<components::Tag>(Entity e) {
    auto* t = world->scene.get_component<components::Tag>(e);
    auto* n = world->scene.get_component<components::Name>(e);

    std::string tag_to_remove = "";
    for (size_t i = 0; i < t->tags.size(); i++) {
        std::string id  = std::string(n->name + "##tag") + std::to_string(i);
        auto&       tag = t->tags[i];

        ImGui::PushID(id.c_str());
        ImGui::InputText("", &tag);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
            tag_to_remove = tag;
        }
        ImGui::PopID();
    }

    if (!tag_to_remove.empty()) {
        std::erase(t->tags, tag_to_remove);
    }

    if (ImGui::Button(ICON_FA_PLUS_CIRCLE)) {
        t->tags.push_back("NewTag");
    }
}

template <> void Editor::render_component_ui<components::Name>(Entity e) {
    auto* n = world->scene.get_component<components::Name>(e);
    ImGui::Text("%s", n->name.c_str());
}

Editor::Editor(World* world, std::unordered_map<uint32_t, VkDescriptorSet>& imgui_material_image_handles)
    : imgui_material_image_handles(imgui_material_image_handles) {
    this->world = world;

    this->register_component<components::Transform>("Transform", false);
    this->register_component<components::Name>("Name", false);

    this->register_component<components::Mesh>("Mesh", true);
    this->register_component<components::Physics>("Physics", true);
    this->register_component<components::Script>("Script", true);
    this->register_component<components::Tag>("Tag", true);
}

void Editor::render_main_menu(std::function<void(std::string)> scene_save_callback) {
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
}

void Editor::render_scene_hierarchy_window() {
    ImGui::Begin(ICON_FA_SITEMAP " Scene Hierarchy");
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Create Node")) {
            world->scene.create_node("New Node");
        }
        ImGui::EndPopup();
    }

    auto view =
        world->scene.entity_registry.view<components::Transform, components::Name>(entt::exclude<components::Parent>);
    for (auto [e, t, n] : view.each()) {
        draw_node_in_hierarchy(e, selected_entity);
    }

    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
        selected_entity = entt::null;
    }
    ImGui::End();
}

void Editor::set_selected_entity(Entity e) {
    this->selected_entity = e;
}

Entity Editor::get_selected_entity() {
    return this->selected_entity;
}

void Editor::draw_node_in_hierarchy(Entity e, Entity& selected_entity) {
    auto children = world->scene.get_component<components::Children>(e);
    auto name     = world->scene.get_component<components::Name>(e);

    ImGuiTreeNodeFlags flags =
        ((selected_entity == e) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
    flags |= ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (!children) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    ImGui::PushID((uint64_t)e);
    bool opened = ImGui::TreeNodeEx(&e, flags, "%s", name->name.c_str());
    ImGui::PopID();

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::BeginMenu("Add Component")) {
            for (auto [component_id, info] : this->node_component_map) {
                if (!this->entity_has_component(component_id, e)) {
                    if (ImGui::MenuItem(info.name.c_str())) {
                        info.add_component(e);
                    }
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Remove Component")) {
            for (auto [component_id, info] : this->node_component_map) {
                if (this->entity_has_component(component_id, e) && info.removable) {
                    if (ImGui::MenuItem(info.name.c_str())) {
                        info.remove_component(e);
                    }
                }
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
                draw_node_in_hierarchy(child, selected_entity);
            }
        }

        ImGui::TreePop();
    }
}

bool Editor::entity_has_component(entt::id_type component_type, Entity entity) {
    auto storage = world->scene.entity_registry.storage(component_type);

    if (storage == nullptr) {
        return false;
    }

    entt::runtime_view view{};

    return view.iterate(*storage).contains(entity);
}

void Editor::render_scene_node_property_window() {
    ImGui::Begin(ICON_FA_WRENCH " Node Properties");
    if (selected_entity != entt::null) {
        for (auto [component_id, info] : this->node_component_map) {
            if (this->entity_has_component(component_id, selected_entity)) {
                ImGui::PushID(component_id);
                if (info.removable) {
                    if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
                        info.remove_component(selected_entity);
                        ImGui::PopID();
                        continue;
                    } else {
                        ImGui::SameLine();
                    }
                }

                if (ImGui::CollapsingHeader(info.name.c_str())) {
                    ImGui::PushID("Widget");
                    info.render_ui(selected_entity);
                    ImGui::PopID();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

void Editor::render_performance_window(const std::vector<std::pair<std::string, PassTiming>>& passes) {
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
        edited = true;

        if (uniform) {
            value.y = value.x;
            value.z = value.x;
        }
    }
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
        edited = true;

        if (uniform) {
            value.x = value.y;
            value.z = value.y;
        }
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.2f, 0.35f, 0.9f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.1f, 0.25f, 0.8f, 1.0f});
    if (ImGui::Button("Z", button_size)) {
        edited = true;

        value.z = reset_to;

        if (uniform) {
            value.y = reset_to;
            value.x = reset_to;
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    if (ImGui::DragFloat("##Z", &value.z, 0.1f, 0.0f, 0.0f, "%.2f")) {
        edited = true;

        if (uniform) {
            value.x = value.z;
            value.y = value.z;
        }
    }
    ImGui::PopItemWidth();
    ImGui::PopStyleVar();
    ImGui::Columns(1);
    ImGui::PopID();

    return edited;
}
