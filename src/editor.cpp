#include "editor.hpp"

#include "component_registry.hpp"
#include "embedded.hpp"
#include "scene_serializer.hpp"
#include "ui.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

void append_node(World* world, const ModelMetadata::NodeDescription& desc, Entity parent) {
    auto node = world->scene.create_node(desc.name);

    auto t      = world->scene.get_component<components::Transform>(node);
    t->position = desc.position;
    t->scale    = desc.scale;
    t->rotation = desc.rotation;

    if (desc.material_id != AssetMetadata::INVALID_METADATA) {
        auto& mat = world->scene.add_component<components::Material>(node);
        mat.id    = desc.material_id;
    }

    if (desc.mesh_id != AssetMetadata::INVALID_METADATA) {
        auto& mesh = world->scene.add_component<components::Mesh>(node);
        mesh.id    = desc.mesh_id;
    }

    if (parent != entt::null) {
        world->scene.set_node_parent(node, parent);
    }

    for (auto& child : desc.children) {
        append_node(world, child, node);
    }
}

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

    ImGui::Text(ICON_FA_CUBE "  Mesh: ");
    ImGui::SameLine();
    auto metadata = world->asset_registry.get_metadata<MeshMetadata>(m->id);
    if (metadata) {
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::MESH).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;

            if (m->id != new_id) {
                m->id  = new_id;
                edited = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::NewLine();

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

template <typename T>
bool edit_optional_property(
    const char* label, std::optional<T>& value, const T& base_value, std::function<bool(const char*, T&)> edit_func
) {
    bool edited = false;

    ImGui::PushID(label);

    bool has_override = value.has_value();
    if (ImGui::Checkbox("##override", &has_override)) {
        if (has_override) {
            value = base_value;
        } else {
            value.reset();
        }
        edited = true;
    }

    ImGui::SameLine();

    if (has_override) {
        T temp = *value;
        if (edit_func(label, temp)) {
            value = temp;
        }
        edited |= ImGui::IsItemDeactivatedAfterEdit();
    } else {
        ImGui::BeginDisabled();
        T temp = base_value;
        edit_func(label, temp);
        ImGui::EndDisabled();
    }

    ImGui::PopID();
    return edited;
}

template <> bool Editor::render_component_ui<components::Material>(Entity e) {
    bool edited = false;

    auto* m = world->scene.get_component<components::Material>(e);

    auto metadata = world->asset_registry.get_metadata<MaterialMetadata>(m->id);
    ImGui::Text(ICON_FA_TH "  Material: ");
    ImGui::SameLine();
    if (metadata) {
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    ImGui::NewLine();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::MATERIAL).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;
            if (m->id != new_id) {
                m->id  = new_id;
                edited = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (!metadata) {
        return edited;
    }

    auto material_index = world->load_material(metadata->id);

    if (material_index == -1) {
        return edited;
    }

    auto& base_material = world->resources.materials[material_index];

    edited |= edit_optional_property<float>(
        "Roughness Factor",
        m->overrides.roughness_factor,
        base_material.roughness_factor,
        [](const char* label, float& value) {
            return ImGui::SliderFloat(label, &value, 0.0f, 1.0f);
        }
    );

    edited |= edit_optional_property<float>(
        "Metallic Factor",
        m->overrides.metallic_factor,
        base_material.metallic_factor,
        [](const char* label, float& value) {
            return ImGui::SliderFloat(label, &value, 0.0f, 1.0f);
        }
    );

    edited |= edit_optional_property<float>(
        "Normal Scale", m->overrides.normal_scale, base_material.normal_scale, [](const char* label, float& value) {
            return ImGui::SliderFloat(label, &value, 0.0f, 1.0f);
        }
    );

    edited |= edit_optional_property<glm::vec4>(
        "Albedo Factor",
        m->overrides.albedo_factor,
        base_material.albedo_factor,
        [](const char* label, glm::vec4& value) {
            return ImGui::ColorEdit4(label, &value.x);
        }
    );

    edited |= edit_optional_property<glm::vec3>(
        "Emissive Factor",
        m->overrides.emissive_factor,
        base_material.emissive_factor,
        [](const char* label, glm::vec3& value) {
            return ImGui::ColorEdit3(label, &value.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        }
    );

    return edited;
}

template <> bool Editor::render_component_ui<components::Sprite>(Entity e) {
    bool edited = false;

    auto* s = world->scene.get_component<components::Sprite>(e);

    auto metadata = world->asset_registry.get_metadata<TextureMetadata>(s->texture_id);
    ImGui::Text(ICON_FA_IMAGE "  Texture: ");
    ImGui::SameLine();
    if (metadata) {
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    ImGui::NewLine();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::TEXTURE).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;
            if (s->texture_id != new_id) {
                s->texture_id = new_id;
                edited        = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::DragFloat2("Size", &s->size.x, 0.01f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat2("Pivot", &s->pivot.x, 0.01f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat4("UV's", &s->uvs.x, 0.01f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::ColorEdit4("Tint", &s->color.x);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    return edited;
}

template <> bool Editor::render_component_ui<components::Text>(Entity e) {
    bool edited = false;

    auto* t = world->scene.get_component<components::Text>(e);

    auto metadata = world->asset_registry.get_metadata<FontMetadata>(t->font_id);
    ImGui::Text(ICON_FA_FONT "  Font: ");
    ImGui::SameLine();
    if (metadata) {
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    ImGui::NewLine();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::FONT).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;
            if (t->font_id != new_id) {
                t->font_id = new_id;
                edited     = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    static std::string temp_string;
    if (temp_string.compare(t->text) != 0) {
        temp_string = t->text;
    }

    if (ImGui::InputText("##text", &temp_string, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (t->text.compare(temp_string) != 0) {
            t->text = temp_string;
            edited  = true;
        }
    }

    ImGui::DragFloat2("Pivot", &t->pivot.x, 0.01f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    return edited;
}

template <> bool Editor::render_component_ui<components::Sound>(Entity e) {
    bool edited = false;

    auto* s = world->scene.get_component<components::Sound>(e);

    auto metadata = world->asset_registry.get_metadata<SoundMetadata>(s->sound_id);
    ImGui::Text(ICON_FA_VOLUME_UP "  Sound: ");
    ImGui::SameLine();
    if (metadata) {
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    ImGui::NewLine();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::SOUND).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;
            if (s->sound_id != new_id) {
                s->sound_id = new_id;
                edited      = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SliderFloat("Volume", &s->volume, 0.0f, 1.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat("Pitch", &s->pitch, 0.01f, 0.1f, 4.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat("Min Distance", &s->min_distance, 0.1f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::DragFloat("Max Distance", &s->max_distance, 0.1f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("Rolloff", &s->rolloff, 0.0f, 1.0f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    if (ImGui::Checkbox("Spatial", &s->spatial)) {
        edited |= true;
    }

    if (ImGui::Checkbox("Auto Play", &s->autoplay)) {
        edited |= true;
    }

    if (ImGui::Checkbox("Loop", &s->loop)) {
        edited |= true;
    }

    return edited;
}

template <> bool Editor::render_component_ui<components::ParticleEffect>(Entity e) {
    bool edited = false;

    auto* p = world->scene.get_component<components::ParticleEffect>(e);

    auto metadata = world->asset_registry.get_metadata<ParticleEffectMetadata>(p->effect_id);
    ImGui::Text(ICON_FA_STAR "  Effect: ");
    ImGui::SameLine();
    if (metadata) {
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    ImGui::NewLine();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::PARTICLE_EFFECT).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;
            if (p->effect_id != new_id) {
                p->effect_id = new_id;
                edited       = true;
                p->dirty     = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (!p->effect.has_value()) {
        return edited;
    }

    if (ImGui::Checkbox("Active", &p->active)) {
        edited |= true;
    }

    if (ImGui::Button("Reload")) {
        p->dirty = true;
    }

    for (int i = 0; i < p->effect->emitters.size(); i++) {
        ImGui::PushID(i);

        auto& cfg     = p->emitter_configs[i];
        auto& emitter = p->effect->emitters[i];

        ImGui::SeparatorText(emitter.name.c_str());
        ImGui::InputInt("Particle Count", (int*)&cfg.max_particles);
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::InputFloat("Emission Rate", &cfg.emission_rate);
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::InputFloat("Emmiter Lifetime", &cfg.emitter_lifetime);
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        if (ImGui::Checkbox("Loop", &cfg.loop)) {
            edited |= true;
        }

        if (ImGui::Checkbox("Additive", &cfg.additive)) {
            edited |= true;
        }

        if (ImGui::Checkbox("Attached", &cfg.attached)) {
            edited |= true;
        }

        if (edited) {
            p->dirty = true;
        }

        ImGui::PopID();
    }

    return edited;
}

template <> bool Editor::render_component_ui<components::Light>(Entity e) {
    bool edited = false;

    auto* l = world->scene.get_component<components::Light>(e);

    if (ImGui::Checkbox("Enabled", (bool*)&l->light.enabled)) {
        edited |= true;
    }

    if (ImGui::Checkbox("Casts Shadow", (bool*)&l->light.casts_shadow)) {
        edited |= true;
    }

    std::vector<std::string> light_types = {
        "Point",
        "Cone",
    };
    int light_type = (int)l->light.type;

    if (ImGui::BeginCombo("Light Type", light_types[light_type].c_str())) {
        for (int i = 0; i < light_types.size(); i++) {
            bool is_selected = (light_types[i] == light_types[light_type]);
            if (ImGui::Selectable(light_types[i].c_str(), is_selected)) {
                l->light.type = static_cast<LightType>(i);
                edited |= true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::DragFloat("Radius", &l->light.radius, 0.1f, 0.1f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::ColorEdit3("Color", &l->light.color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::InputFloat("Lumens", &l->light.color.w);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    switch (l->light.type) {
    case LightType::POINT:
        break;
    case LightType::SPOT:
        ImGui::DragFloat("Inner cone angle", &l->light.inner_cone_angle, 0.5f, 1.0f, 89.0f, "%.1f deg");
        edited |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::DragFloat("Outer cone angle", &l->light.outer_cone_angle, 0.5f, 1.0f, 89.0f, "%.1f deg");
        edited |= ImGui::IsItemDeactivatedAfterEdit();
        break;
    }

    auto metadata = world->asset_registry.get_metadata<IESProfileMetadata>(l->ies_profile);
    ImGui::Text(ICON_FA_CIRCLE "  IES Profile: ");
    ImGui::SameLine();
    if (metadata) {
        if (ImGui::Button(ICON_FA_REDO)) {
            l->ies_profile = AssetMetadata::INVALID_METADATA;
            edited |= true;
        }
        ImGui::SameLine();
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    ImGui::NewLine();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::IES_PROFILE).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;
            if (l->ies_profile != new_id) {
                l->ies_profile = new_id;
                edited         = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    return edited;
}

template <> bool Editor::render_component_ui<components::SkeletalAnimation>(Entity e) {
    bool edited = false;

    auto* s = world->scene.get_component<components::SkeletalAnimation>(e);

    auto metadata = world->asset_registry.get_metadata<AnimationMetadata>(s->animation_id);
    ImGui::Text(ICON_FA_STAR "  Animation: ");
    ImGui::SameLine();
    if (metadata) {
        ImGui::Text("%s", metadata->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }
    ImGui::NewLine();

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(get_asset_info(AssetType::ANIMATION).drag_drop_id.c_str());
        if (payload) {
            AssetID new_id = *(AssetID*)payload->Data;
            if (s->animation_id != new_id) {
                s->animation_id = new_id;

                auto anim_meta = world->asset_registry.get_metadata<AnimationMetadata>(new_id);
                if (anim_meta) {
                    s->skeleton_id = anim_meta->skeleton_id;
                }

                edited = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    auto skel_meta = world->asset_registry.get_metadata<SkeletonMetadata>(s->skeleton_id);
    ImGui::Text(ICON_FA_BONE "  Skeleton: ");
    ImGui::SameLine();
    if (skel_meta) {
        ImGui::Text("%s", skel_meta->source_path.c_str());
    } else {
        ImGui::Text("Invalid");
    }

    if (ImGui::Checkbox("Loop", &s->looping)) {
        edited |= true;
    }

    ImGui::DragFloat("Animation Speed", &s->speed, 0.1f);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    return edited;
}

template <> bool Editor::render_component_ui<components::DirectionalLight>(Entity e) {
    bool edited = false;

    auto* l = world->scene.get_component<components::DirectionalLight>(e);

    if (ImGui::Checkbox("Enabled", &l->enabled)) {
        edited |= true;
    }

    ImGui::ColorEdit3("Color", &l->color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::InputFloat("Lux", &l->color.w);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    return edited;
}

template <> bool Editor::render_component_ui<components::Sky>(Entity e) {
    bool edited = false;

    auto* s = world->scene.get_component<components::Sky>(e);

    ImGui::ColorEdit3(
        "Top Hemisphere Color", &s->top_hemisphere_color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
    );
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::InputFloat("Top Hemisphere Lux", &s->top_hemisphere_color.w);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::ColorEdit3(
        "Bottom Hemisphere Color", &s->bottom_hemisphere_color.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
    );
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::InputFloat("Bottom Hemisphere Lux", &s->bottom_hemisphere_color.w);
    edited |= ImGui::IsItemDeactivatedAfterEdit();

    return edited;
}

Editor::Editor() {
    asset_type_infos = {
        {AssetType::MATERIAL,
         AssetTypeInfo{
             .icon          = ICON_FA_TH,
             .category_name = "Materials",
             .drag_drop_id  = "DRAG_DROP_MATERIAL",
         }},
        {AssetType::MESH,
         AssetTypeInfo{
             .icon          = ICON_FA_CUBE,
             .category_name = "Meshes",
             .drag_drop_id  = "DRAG_DROP_MESH",
         }},
        {AssetType::MODEL,
         AssetTypeInfo{
             .icon          = ICON_FA_CUBES,
             .category_name = "Models",
             .drag_drop_id  = "DRAG_DROP_MODEL",
         }},
        {AssetType::SCRIPT,
         AssetTypeInfo{
             .icon          = ICON_FA_SCROLL,
             .category_name = "Scripts",
             .drag_drop_id  = "DRAG_DROP_SCRIPT",
         }},
        {AssetType::SHADER,
         AssetTypeInfo{
             .icon          = ICON_FA_LIGHTBULB,
             .category_name = "Shaders",
             .drag_drop_id  = "DRAG_DROP_SHADER",
         }},
        {AssetType::SOUND,
         AssetTypeInfo{
             .icon          = ICON_FA_VOLUME_UP,
             .category_name = "Sounds",
             .drag_drop_id  = "DRAG_DROP_SOUND",
         }},
        {AssetType::TEXTURE,
         AssetTypeInfo{
             .icon          = ICON_FA_IMAGE,
             .category_name = "Textures",
             .drag_drop_id  = "DRAG_DROP_TEXTURE",
         }},
        {AssetType::FONT,
         AssetTypeInfo{
             .icon          = ICON_FA_FONT,
             .category_name = "Fonts",
             .drag_drop_id  = "DRAG_DROP_FONT",
         }},
        {AssetType::PARTICLE_EFFECT,
         AssetTypeInfo{
             .icon          = ICON_FA_STAR,
             .category_name = "Particle Effects",
             .drag_drop_id  = "DRAG_DROP_PARTICLE_EFFECT",
         }},
        {AssetType::ANIMATION,
         AssetTypeInfo{
             .icon          = ICON_FA_STAR,
             .category_name = "Skeletal Animations",
             .drag_drop_id  = "DRAG_DROP_SKELETAL_ANIMATION",
         }},
        {AssetType::SKELETON,
         AssetTypeInfo{
             .icon          = ICON_FA_BONE,
             .category_name = "Skeletons",
             .drag_drop_id  = "DRAG_DROP_SKELETON",
         }},
        {AssetType::IES_PROFILE,
         AssetTypeInfo{
             .icon          = ICON_FA_CIRCLE,
             .category_name = "IES Profiles",
             .drag_drop_id  = "DRAG_DROP_IES_PROFILE",
         }},
        {AssetType::UNSUPPORTED,
         AssetTypeInfo{
             .icon          = ICON_FA_QUESTION,
             .category_name = "Unsupported",
             .drag_drop_id  = "DRAG_DROP_UNSUPPORTED",
         }},
    };
}

void Editor::initialize(World* world) {
    this->world = world;

    icon_font = generate_icon_font(48.0f);

    asset_importer.initialize(world);
    asset_exporter.initialize(world);
}

bool Editor::render_main_menu() {
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

            if (ImGui::MenuItem("Bake Static Collision Shapes")) {
                auto view = world->scene.entity_registry.view<components::Transform, components::Mesh>(
                    entt::exclude_t<components::Physics>()
                );

                for (auto [e, t, m] : view.each()) {
                    JPH::TriangleList triangles;
                    if (!world->load_collision_mesh(m.id, triangles)) {
                        continue;
                    }

                    JPH::MeshShapeSettings mesh_settings(triangles);
                    JPH::ShapeRefC         collision_shape = mesh_settings.Create().Get();

                    JPH::ShapeRefC final_shape;
                    float          scale = t.world_scale;
                    if (scale != 1.0f) {
                        JPH::ScaledShapeSettings scaled(collision_shape, JPH::Vec3::sReplicate(scale));
                        final_shape = scaled.Create().Get();
                    } else {
                        final_shape = collision_shape;
                    }

                    JPH::BodyInterface& body_interface = world->physics.system.GetBodyInterface();

                    auto                      pos = t.world_position;
                    auto                      rot = t.world_rotation;
                    JPH::BodyCreationSettings body_settings(
                        final_shape,
                        JPH::RVec3(pos.x, pos.y, pos.z),
                        JPH::Quat(rot.x, rot.y, rot.z, rot.w),
                        JPH::EMotionType::Static,
                        Layers::NON_MOVING
                    );

                    JPH::BodyID body_id =
                        body_interface.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);

                    auto& p      = world->scene.add_component<components::Physics>(e);
                    p.body_id    = body_id;
                    p.is_static  = true;
                    p.last_scale = scale;
                }
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
        ImGui::Text("Save Scene?");
        ImGui::Separator();

        if (ImGui::Button("Save")) {
            SceneSerializer::save(world->asset_registry.root_path(), *world);
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
    ImGui::SetNextWindowSize(ImVec2(200, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin(ICON_FA_CLOCK " Performance");
    if (ImGui::BeginTable(
            "PassStats",
            3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable |
                ImGuiTableFlags_SizingStretchProp
        )) {
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
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.2f FPS", 1000.0f / total_avg);
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

void Editor::on_files_dropped(const std::queue<std::string>& paths) {
    import_queue = paths;
}

void Editor::render_asset_importer() {
    if (!import_queue.empty() && !import_dialog_open) {
        auto path = import_queue.front();
        import_queue.pop();

        import_asset_type = world->asset_registry.extension_to_asset_type(path);
        import_asset_path = path;

        ImGui::OpenPopup("AssetImportPopup");
        import_dialog_open = true;
    }

    if (ImGui::BeginPopupModal("AssetImportPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Importing Asset: %s", import_asset_path.filename().string().c_str());
        ImGui::NewLine();

        switch (import_asset_type) {
        case AssetType::UNSUPPORTED:
            spdlog::warn("Unsupported file: {}", import_asset_path.string());
            ImGui::CloseCurrentPopup();
            import_dialog_open = false;
            break;
        case AssetType::TEXTURE:
            render_texture_import_dialog(texture_import_options);
            break;
        case AssetType::MESH:
            render_mesh_import_dialog(mesh_import_options);
            break;
        case AssetType::MODEL:
            render_model_import_dialog(model_import_options);
            break;
        case AssetType::SOUND:
            render_sound_import_dialog(sound_import_options);
            break;
        case AssetType::IES_PROFILE:
            break;
        case AssetType::MATERIAL:
        case AssetType::SHADER:
        case AssetType::SCRIPT:
        case AssetType::PARTICLE_EFFECT:
        case AssetType::ANIMATION:
        case AssetType::SKELETON:
            spdlog::warn("Importer for {} is not implemented", import_asset_path.string());
            ImGui::CloseCurrentPopup();
            import_dialog_open = false;
            break;
        case AssetType::FONT:
            render_font_import_dialog(font_import_options);
            break;
        }

        ImGui::NewLine();
        if (ImGui::Button("Import Asset")) {
            switch (import_asset_type) {
            case AssetType::UNSUPPORTED:
                break;
            case AssetType::TEXTURE:
                asset_importer.import_texture(import_asset_path, texture_import_options);
                break;
            case AssetType::MESH:
            case AssetType::MODEL:
                asset_importer.import_model(import_asset_path, model_import_options);
                break;
            case AssetType::SOUND:
                asset_importer.import_sound(import_asset_path, sound_import_options);
                break;
            case AssetType::IES_PROFILE:
                asset_importer.import_ies_profile(import_asset_path);
                break;
            case AssetType::MATERIAL:
            case AssetType::SHADER:
            case AssetType::SCRIPT:
            case AssetType::PARTICLE_EFFECT:
            case AssetType::ANIMATION:
            case AssetType::SKELETON:
                break;
            case AssetType::FONT:
                asset_importer.import_font(import_asset_path, font_import_options);
                break;
            }

            ImGui::CloseCurrentPopup();
            import_dialog_open = false;
        }

        ImGui::SameLine();

        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
            import_dialog_open = false;
        }

        ImGui::EndPopup();
    }
}

bool Editor::render_asset_explorer() {
    static AssetType selected_type  = AssetType::UNSUPPORTED;
    static AssetID   selected_asset = AssetMetadata::INVALID_METADATA;

    std::unordered_map<AssetType, std::vector<const AssetMetadata*>> assets;
    for (auto& [id, handle] : world->asset_registry.get_metadata_store()) {
        auto it = assets.find(handle->type);
        if (it == assets.end()) {
            assets[handle->type] = {};
        }
        assets[handle->type].push_back(handle.get());
    }

    ImGui::Begin(ICON_FA_FILE " Assets");
    if (ImGui::BeginTable("asset_explorer_table", 3, ImGuiTableFlags_Resizable)) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("asset_explorer_tree");
        for (auto& [type, asset] : assets) {
            AssetTypeInfo info = get_asset_info(type);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf;
            if (selected_type == type) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            if (ImGui::TreeNodeEx((info.icon + "  " + info.category_name).c_str(), flags)) {
                ImGui::TreePop();
            }

            if (ImGui::IsItemClicked()) {
                selected_type = type;
            }
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("asset_explorer_grid");

        AssetTypeInfo info = get_asset_info(selected_type);

        float column_width = ImGui::GetContentRegionAvail().x;
        int   colum_count  = glm::max(1, (int)(column_width / (asset_icon_width + 20.0f)));

        bool open_new_particle_effect_popup = false;

        if (ImGui::BeginTable("asset_explorer_grid_inner", colum_count)) {
            for (const auto asset : assets[selected_type]) {
                ImGui::TableNextColumn();

                bool selected = selected_asset == asset->id;
                if (draw_asset(
                        info.icon.c_str(),
                        world->asset_registry.relative_path(asset->source_path).string().c_str(),
                        info.drag_drop_id.c_str(),
                        asset->id,
                        asset->type,
                        selected
                    )) {
                    selected_asset = asset->id;
                }
            }

            if (ImGui::BeginPopupContextWindow(
                    "##asset_panel_context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems
                )) {
                if (selected_type == AssetType::PARTICLE_EFFECT) {
                    if (ImGui::MenuItem("New Particle Effect")) {
                        open_new_particle_effect_popup = true;
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::EndTable();
        }

        if (open_new_particle_effect_popup) {
            ImGui::OpenPopup("NewParticleEffect");
        }

        if (ImGui::BeginPopup("NewParticleEffect")) {
            static std::string name_buf;
            if (ImGui::IsWindowAppearing()) {
                name_buf = "";
            }
            ImGui::SetNextItemWidth(200.0f);
            bool confirm = ImGui::InputText("Name", &name_buf, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            confirm |= ImGui::Button("Create");
            if (confirm && !name_buf.empty()) {
                asset_exporter.export_particle_effect({}, {}, name_buf);

                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();

        ImGui::TableSetColumnIndex(2);
        ImGui::BeginChild("asset_explorer_info");
        auto selected_metadata = world->asset_registry.get_metadata<AssetMetadata>(selected_asset);
        if (selected_metadata && selected_metadata->is_valid()) {
            ImGui::Text("ID: %lu", selected_metadata->id);
            ImGui::Text("Source Path: %s", selected_metadata->source_path.c_str());
            ImGui::Text("Asset Path: %s", selected_metadata->asset_path.c_str());
            ImGui::Text("Type: %lu", selected_metadata->type);
            ImGui::Text("Source Timestamp: %lu", selected_metadata->source_timestamp);
            ImGui::Text("Import Timestamp: %lu", selected_metadata->imported_timestamp);
            ImGui::Text("Is Standalone: %s", selected_metadata->standalone ? "Yes" : "No");
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }
    ImGui::End();

    return false;
}

void Editor::render_texture_import_dialog(TextureMetadata::TextureImportOptions& options) {
    ImGui::SeparatorText("Texture Import Options");
    ImGui::Checkbox("sRGB Tetxure", &options.is_srgb);
    ImGui::Checkbox("Normal Map", &options.is_normal_map);
    ImGui::Checkbox("Generate Mipmaps", &options.generate_mipmaps);

    // NOTE: ugly way of doing this
    std::vector<std::string> compression_options = {
        "None",
        "BC5",
        "BC7",
    };
    int compression_index = (int)options.compression;

    if (ImGui::BeginCombo("Compression", compression_options[compression_index].c_str())) {
        for (int i = 0; i < compression_options.size(); i++) {
            bool is_selected = (compression_options[i] == compression_options[compression_index]);
            if (ImGui::Selectable(compression_options[i].c_str(), is_selected)) {
                options.compression = static_cast<TextureMetadata::TextureImportOptions::Compression>(i);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    {
        std::unordered_map<VkFilter, std::string> filter_options = {
            {VK_FILTER_LINEAR, "LINEAR"},
            {VK_FILTER_NEAREST, "NEAREST"},
        };
        auto filter = options.sampler_description.filter;

        if (ImGui::BeginCombo("Filter", filter_options[filter].c_str())) {
            for (auto& [mode, name] : filter_options) {
                bool is_selected = (filter == mode);
                if (ImGui::Selectable(name.c_str(), is_selected)) {
                    options.sampler_description.filter = mode;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    {
        std::unordered_map<VkSamplerAddressMode, std::string> address_options = {
            {VK_SAMPLER_ADDRESS_MODE_REPEAT, "REPEAT"},
            {VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT, "MIRRORED REPEAT"},
            {VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, "CLAMP TO EDGE"},
            {VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, "CLAMP TO BORDER"},
            {VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE, "MIRROR CLAMP TO EDGE"},
        };
        auto address = options.sampler_description.address_mode;

        if (ImGui::BeginCombo("Address Mode", address_options[address].c_str())) {
            for (auto& [mode, name] : address_options) {
                bool is_selected = (address == mode);
                if (ImGui::Selectable(name.c_str(), is_selected)) {
                    options.sampler_description.address_mode = mode;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    {
        std::unordered_map<VkSamplerMipmapMode, std::string> mipmap_options = {
            {VK_SAMPLER_MIPMAP_MODE_LINEAR, "LINEAR"},
            {VK_SAMPLER_MIPMAP_MODE_NEAREST, "NEAREST"},
        };
        auto mipmap_mode = options.sampler_description.mipmap_mode;

        if (ImGui::BeginCombo("Mipmap mode", mipmap_options[mipmap_mode].c_str())) {
            for (auto& [mode, name] : mipmap_options) {
                bool is_selected = (mipmap_mode == mode);
                if (ImGui::Selectable(name.c_str(), is_selected)) {
                    options.sampler_description.mipmap_mode = mipmap_mode;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::SliderFloat("Anisotropy", &options.sampler_description.anisotropy, 0.0f, 16.0f)) {
        options.sampler_description.anisotropy = glm::ceil(options.sampler_description.anisotropy);
    }
}

void Editor::render_mesh_import_dialog(MeshMetadata::MeshImportOptions& options) {
    ImGui::SeparatorText("Mesh Import Options");

    ImGui::Checkbox("Generate LOD's", &options.generate_lods);
    ImGui::Checkbox("Generate Meshlets", &options.generate_meshlets);
}

void Editor::render_model_import_dialog(ModelMetadata::ModelImportOptions& options) {
    render_mesh_import_dialog(options.mesh_import_options);
    render_texture_import_dialog(options.texture_import_options);
}

void Editor::render_font_import_dialog(FontMetadata::FontImportOptions& options) {
    ImGui::InputFloat("Font Size", &options.font_size);
    ImGui::Checkbox("SDF Font", &options.is_sdf);
}

void Editor::render_sound_import_dialog(SoundMetadata::SoundImportOptions& options) {
    ImGui::Checkbox("Stream", &options.stream);
}

bool Editor::draw_asset(
    const char* icon, const char* label, const char* drag_drop_id, AssetID asset_id, AssetType type, bool& selected
) {
    ImGui::PushID(label);

    ImVec2 cursor_pos = ImGui::GetCursorPos();

    bool clicked = ImGui::InvisibleButton("##asset", ImVec2(asset_icon_width, asset_icon_height));

    if (type == AssetType::MODEL) {
        auto asset = world->asset_registry.get_metadata<ModelMetadata>(asset_id);
        if (asset != nullptr) {
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Append scene to world")) {
                    for (auto& desc : asset->scene_description.nodes) {
                        append_node(world, desc, entt::null);
                    }
                }
                ImGui::EndPopup();
            }
        }
    }
    if (type == AssetType::PARTICLE_EFFECT) {
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Edit")) {
                auto asset = world->asset_registry.get_metadata<ParticleEffectMetadata>(asset_id);
                if (asset != nullptr) {
                    std::ifstream            file(asset->source_path);
                    cereal::JSONInputArchive archive(file);

                    ParticleEffectSaveData data;
                    archive(data);

                    particle_editor.load(data, std::filesystem::path(asset->source_path).stem().string());
                };
            }
            ImGui::EndPopup();
        }
    }
    bool hovered = ImGui::IsItemHovered();

    if (clicked) {
        selected = !selected;
    }

    if (hovered || selected) {
        ImVec2 p1 = ImGui::GetItemRectMin();
        ImVec2 p2 = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            p1, p2, ImGui::GetColorU32(selected ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered), 4.0f
        );
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload(drag_drop_id, &asset_id, sizeof(AssetID));
        ImGui::Text("%s", label);
        ImGui::EndDragDropSource();
    }

    ImGui::SetCursorPos(cursor_pos);

    ImGui::PushFont(icon_font);
    ImVec2 icon_size = ImGui::CalcTextSize(icon);
    ImGui::SetCursorPosX(cursor_pos.x + (asset_icon_width - icon_size.x) * 0.5f);
    ImGui::Text("%s", icon);
    ImGui::PopFont();

    ImGui::SetCursorPosX(cursor_pos.x);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + asset_icon_width);

    ImVec2 text_size = ImGui::CalcTextSize(label, NULL, false, asset_icon_width);
    if (text_size.x < asset_icon_width) {
        ImGui::SetCursorPosX(cursor_pos.x + (asset_icon_width - text_size.x) * 0.5f);
    }

    ImGui::TextWrapped("%s", label);
    ImGui::PopTextWrapPos();

    ImGui::PopID();

    return clicked;
}

Editor::AssetTypeInfo Editor::get_asset_info(AssetType type) {
    auto it = asset_type_infos.find(type);
    if (it != asset_type_infos.end()) {
        return it->second;
    }

    return {};
}

void Editor::render_particle_editor() {
    ImGui::Begin(ICON_FA_STAR " Particle Editor");
    particle_editor.render(&asset_exporter);
    ImGui::End();
}

bool Editor::handle_delete() {
    if (ax::NodeEditor::GetCurrentEditor() && ax::NodeEditor::IsActive()) {
        return false;
    } else {
        return true;
    }
}
