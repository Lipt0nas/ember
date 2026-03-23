#pragma once

#include "asset_exporter.hpp"
#include "asset_importer.hpp"
#include "ember.hpp"
#include "framegraph.hpp"
#include "particle_editor.hpp"
#include "scene.hpp"
#include "script_system.hpp"
#include "world.hpp"

#include <queue>

struct SceneNodeComponentInfo {
    std::string name;

    bool removable;

    std::function<void(Entity)> render_ui;
    std::function<void(Entity)> add_component;
    std::function<void(Entity)> remove_component;
};

class Editor {
public:
    Editor();

    void initialize(World* world);

    void on_files_dropped(const std::queue<std::string>& paths);

    void render_asset_importer();
    bool render_asset_explorer();
    bool render_main_menu();
    bool render_scene_hierarchy_window();
    bool render_scene_node_property_window();
    bool render_performance_window(const std::vector<std::pair<std::string, PassTiming>>& passes);
    void render_particle_editor();

    bool handle_delete();

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

    static constexpr float asset_icon_width  = 80.0f;
    static constexpr float asset_icon_height = 100.0f;
    bool                   draw_asset(
                          const char* icon, const char* label, const char* drag_drop_id, AssetID asset_id, AssetType type, bool& selected
                      );

    std::queue<std::string> import_queue;
    bool                    import_dialog_open = false;

    AssetType             import_asset_type = AssetType::UNSUPPORTED;
    std::filesystem::path import_asset_path;

    TextureMetadata::TextureImportOptions texture_import_options;
    MeshMetadata::MeshImportOptions       mesh_import_options;
    ModelMetadata::ModelImportOptions     model_import_options;
    FontMetadata::FontImportOptions       font_import_options;
    SoundMetadata::SoundImportOptions     sound_import_options;

    void render_texture_import_dialog(TextureMetadata::TextureImportOptions& options);
    void render_mesh_import_dialog(MeshMetadata::MeshImportOptions& options);
    void render_model_import_dialog(ModelMetadata::ModelImportOptions& options);
    void render_font_import_dialog(FontMetadata::FontImportOptions& options);
    void render_sound_import_dialog(SoundMetadata::SoundImportOptions& options);

    ParticleEditor particle_editor;

    Entity        selected_entity = entt::null;
    World*        world           = nullptr;
    AssetImporter asset_importer;
    AssetExporter asset_exporter;
    ImFont*       icon_font = nullptr;

    struct AssetTypeInfo {
        std::string icon          = "?";
        std::string category_name = "Unknown";
        std::string drag_drop_id  = "unknown";
    };

    std::unordered_map<AssetType, AssetTypeInfo> asset_type_infos;
    AssetTypeInfo                                get_asset_info(AssetType type);
};

template <> bool Editor::render_component_ui<components::Transform>(Entity e);
template <> bool Editor::render_component_ui<components::Mesh>(Entity e);
template <> bool Editor::render_component_ui<components::Script>(Entity e);
template <> bool Editor::render_component_ui<components::Name>(Entity e);
template <> bool Editor::render_component_ui<components::Physics>(Entity e);
template <> bool Editor::render_component_ui<components::Tag>(Entity e);
template <> bool Editor::render_component_ui<components::Camera>(Entity e);
template <> bool Editor::render_component_ui<components::CharacterController>(Entity e);
template <> bool Editor::render_component_ui<components::Material>(Entity e);
template <> bool Editor::render_component_ui<components::Sprite>(Entity e);
template <> bool Editor::render_component_ui<components::Text>(Entity e);
template <> bool Editor::render_component_ui<components::Sound>(Entity e);
template <> bool Editor::render_component_ui<components::ParticleEffect>(Entity e);
template <> bool Editor::render_component_ui<components::Light>(Entity e);
template <> bool Editor::render_component_ui<components::SkeletalAnimation>(Entity e);
template <> bool Editor::render_component_ui<components::DirectionalLight>(Entity e);
template <> bool Editor::render_component_ui<components::Sky>(Entity e);
