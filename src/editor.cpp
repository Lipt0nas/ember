#include "editor.hpp"

#include "embedded.hpp"
#include "imgui_internal.h"

#include <imgui.h>

Editor::Editor() {
    this->register_component<components::Transform>("Transform", false);
    this->register_component<components::Name>("Name", false);

    this->register_component<components::Mesh>("Mesh", true);
    this->register_component<components::Physics>("Physics", true);
    this->register_component<components::Script>("Script", true);
}

void Editor::render_scene_hierarchy_window(Scene& scene) {
    ImGui::Begin(ICON_FA_SITEMAP " Scene Hierarchy");
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Create Node")) {
            scene.create_entity("New Node");
        }
        ImGui::EndPopup();
    }

    auto view = scene.entity_registry.view<components::Transform, components::Name>(entt::exclude<components::Parent>);
    for (auto [e, t, n] : view.each()) {
        draw_node_in_hierarchy(scene, e, selected_entity);
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

void Editor::draw_node_in_hierarchy(Scene& scene, Entity e, Entity& selected_entity) {
    auto children = scene.get_component<components::Children>(e);
    auto name     = scene.get_component<components::Name>(e);

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
                if (!this->entity_has_component(scene, component_id, e)) {
                    if (ImGui::MenuItem(info.name.c_str())) {
                        info.add_component(scene, e);
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
                draw_node_in_hierarchy(scene, child, selected_entity);
            }
        }

        ImGui::TreePop();
    }
}

bool Editor::entity_has_component(Scene& scene, entt::id_type component_type, Entity entity) {
    auto storage = scene.entity_registry.storage(component_type);

    if (storage == nullptr) {
        return false;
    }

    entt::runtime_view view{};

    return view.iterate(*storage).contains(entity);
}

void Editor::render_scene_node_property_window(
    Scene&                                               scene,
    ScriptSystem&                                        script_system,
    const std::unordered_map<uint32_t, VkDescriptorSet>& imgui_material_image_handles
) {
    ImGui::Begin(ICON_FA_WRENCH " Node Properties");
    if (selected_entity != entt::null) {
        auto* t = scene.get_component<components::Transform>(selected_entity);
        if (t) {
            if (ImGui::CollapsingHeader("Transform")) {
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
                        glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(temp_rotation.x), glm::vec3(1, 0, 0)) *
                        t->rotation;
                    t->rotation =
                        glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(temp_rotation.y), glm::vec3(0, 1, 0)) *
                        t->rotation;
                    t->rotation =
                        glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(temp_rotation.z), glm::vec3(0, 0, 1)) *
                        t->rotation;
                }

                ImGui::SeparatorText("World Transform");
                glm::vec3 temp_position = t->world_position;
                draw_vec3_controls("World Position", temp_position);

                temp_scale = glm::vec3(t->world_scale);
                draw_vec3_controls("World Scale", temp_scale, 1.0f, true);

                temp_rotation = glm::vec3(t->world_rotation.x, t->world_rotation.y, t->world_rotation.z);
                draw_vec3_controls("World Rotation", temp_rotation);
            }
        }

        auto* m = scene.get_component<components::Mesh>(selected_entity);
        if (m) {
            if (ImGui::CollapsingHeader("Material")) {
                auto& material = scene.materials[m->mesh.material_id];

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
            }
        }

        auto* s = scene.get_component<components::Script>(selected_entity);
        if (s) {
            if (ImGui::CollapsingHeader("Script")) {
                auto scripts = script_system.get_scripts();

                std::string current_name = "None";
                if (scripts.contains(s->script_id)) {
                    current_name = scripts.at(s->script_id).name;
                }

                if (ImGui::BeginCombo("Script Source", current_name.c_str())) {
                    for (auto& [id, script] : scripts) {
                        if (ImGui::Selectable(script.name.c_str(), id == s->script_id)) {
                            s->script_id = id;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }

        auto* n = scene.get_component<components::Name>(selected_entity);
        if (n) {
            if (ImGui::CollapsingHeader("Name")) {
            }
        }

        auto* p = scene.get_component<components::Parent>(selected_entity);
        if (p) {
            if (ImGui::CollapsingHeader("Parent")) {
            }
        }

        auto* c = scene.get_component<components::Children>(selected_entity);
        if (c) {
            if (ImGui::CollapsingHeader("Children")) {
            }
        }

        auto* ph = scene.get_component<components::Physics>(selected_entity);
        if (ph) {
            if (ImGui::CollapsingHeader("Physics")) {
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
