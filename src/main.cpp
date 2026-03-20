#include "ember.hpp"

#include "args.hpp"
#include "camera.hpp"
#include "component_registry.hpp"
#include "device.hpp"
#include "editor.hpp"
#include "embedded.hpp"
#include "framegraph.hpp"
#include "geometry.hpp"
#include "imgui_internal.h"
#include "input_system.hpp"
#include "physics.hpp"
#include "pipeline.hpp"
#include "resources.hpp"
#include "rt_scene.hpp"
#include "scene.hpp"
#include "scene_serializer.hpp"
#include "script_system.hpp"
#include "swapchain.hpp"
#include "ui.hpp"
#include "world.hpp"

#include <deque>

#include <tracy/Tracy.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include <ImGuizmo.h>

struct EditorViewportSource {
    Image           image;
    VkDescriptorSet descriptor_set;
};

class SceneHistory {
public:
    void create_snapshot(World& world) {
        if (world.is_running) {
            return;
        }

        std::stringstream snapshot_stream;

        cereal::BinaryOutputArchive output(snapshot_stream);
        entt::snapshot              snapshot(world.scene.entity_registry);
        snapshot.get<entt::entity>(output);
        ComponentRegistry::save_snapshot(snapshot, output);

        while (newest_snapshot != 0) {
            if (snapshots.empty()) {
                break;
            }
            snapshots.pop_front();
            newest_snapshot--;
        }

        if (snapshots.size() >= MAX_SNAPHSOTS) {
            snapshots.pop_back();
        }

        snapshots.push_front(std::move(snapshot_stream));
        newest_snapshot = 0;
        spdlog::debug("Saved snapshot {} size: {}MB", newest_snapshot, snapshot_stream.view().size() / 1024 / 1024);
    }

    void load_snapshot(World& world) {
        if (world.is_running) {
            return;
        }

        if (snapshots.empty()) {
            return;
        }

        load_snapshot_at(world, newest_snapshot);
    }

    bool undo(World& world) {
        if (world.is_running) {
            return false;
        }

        if (snapshots.empty() || newest_snapshot == snapshots.size() - 1) {
            return false;
        }

        newest_snapshot += 1;
        load_snapshot_at(world, newest_snapshot);

        return true;
    }

    bool redo(World& world) {
        if (world.is_running) {
            return false;
        }

        if (snapshots.empty() || newest_snapshot == 0) {
            return false;
        }

        newest_snapshot -= 1;
        load_snapshot_at(world, newest_snapshot);

        return true;
    }

    int snapshot_count() {
        return snapshots.size();
    }

private:
    void load_snapshot_at(World& world, int snapshot) {
        world.scene.entity_registry.clear();

        auto& stream = snapshots.at(snapshot);

        spdlog::debug("Loading snapshot {} size: {}MB", snapshot, snapshots.at(snapshot).view().size() / 1024 / 1024);

        cereal::BinaryInputArchive input(stream);
        entt::snapshot_loader      loader(world.scene.entity_registry);
        loader.get<entt::entity>(input);
        ComponentRegistry::load_snapshot(loader, input);

        stream.seekg(0);
    }

    static constexpr int MAX_SNAPHSOTS = 20;

    std::deque<std::stringstream> snapshots;

    int newest_snapshot = 0;
};

void update_transform_hierarchy(World& world) {
    ZoneScopedN("Update Transforms");
    auto root_view = world.scene.entity_registry.view<components::Transform>(entt::exclude<components::Parent>);
    for (auto [e, t] : root_view.each()) {
        t.world_position = t.position;
        t.world_scale    = t.scale;
        t.world_rotation = t.rotation;
    }

    bool                       has_updates = true;
    std::unordered_set<Entity> processed;

    for (auto e : root_view) {
        processed.insert(e);
    }

    while (has_updates) {
        has_updates = false;

        auto child_view = world.scene.entity_registry.view<components::Transform, components::Parent>();
        for (auto [e, ct, p] : child_view.each()) {
            if (processed.contains(e)) {
                continue;
            }

            if (!processed.contains(p.parent)) {
                continue;
            }

            auto& parent_transform = world.scene.entity_registry.get<components::Transform>(p.parent);

            ct.world_rotation = parent_transform.world_rotation * ct.rotation;
            ct.world_scale    = parent_transform.world_scale * ct.scale;
            ct.world_position = parent_transform.world_position +
                                (parent_transform.world_rotation * (ct.position * parent_transform.scale));

            processed.insert(e);
            has_updates = true;
        }
    }

    auto view = world.scene.entity_registry.view<components::Transform, components::Mesh>();
    for (auto [e, t, m] : view.each()) {
        m.instance.position = t.world_position;
        m.instance.scale    = t.world_scale;
        m.instance.rotation = t.world_rotation;
    }
}

void fill_drawcalls(
    World& world, std::vector<MeshInstance>& mesh_instances, std::vector<Entity>& mesh_instance_entities
) {
    ZoneScopedN("Fill Drawcalls");

    world.renderer.skinned_mesh_count = 0;
    world.renderer.static_mesh_count  = 0;

    mesh_instances.clear();
    mesh_instance_entities.clear();

    auto mat_view = world.scene.entity_registry.view<components::Material>();
    for (auto [e, mat] : mat_view.each()) {
        auto mat_index = world.load_material(mat.id);
        if (mat_index != -1) {
            if (mat.overrides.active()) {
                if (mat.dedicated_material_index == -1) {
                    mat.dedicated_material_index = world.dedicate_material(mat, mat_index);
                }
                world.apply_material_override(mat);
            }
        }
    }

    auto view = world.scene.entity_registry.view<components::Mesh, components::Material>(
        entt::exclude<components::SkeletalAnimation>
    );
    for (auto [e, mesh, mat] : view.each()) {
        if (mesh.id == AssetMetadata::INVALID_METADATA || mat.id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        auto mat_index = world.load_material(mat.id);
        if (mat_index != -1) {
            mesh.instance.material_id = mat.dedicated_material_index == -1
                                            ? mat_index
                                            : world.resources.materials.size() + mat.dedicated_material_index;
        }

        auto mesh_index = world.load_mesh(mesh.id);
        if (mesh_index != -1) {
            mesh.instance.mesh_id = mesh_index;
        }

        world.renderer.static_mesh_count++;
        mesh_instances.push_back(mesh.instance);
        mesh_instance_entities.push_back(e);
    }

    uint32_t output_offset = 0;
    auto     anim_view =
        world.scene.entity_registry.view<components::Mesh, components::Material, components::SkeletalAnimation>();
    for (auto [e, mesh, mat, a] : anim_view.each()) {
        if (a.skeleton_id == AssetMetadata::INVALID_METADATA || a.animation_id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        if (mesh.id == AssetMetadata::INVALID_METADATA || mat.id == AssetMetadata::INVALID_METADATA) {
            continue;
        }

        auto mat_index = world.load_material(mat.id);
        if (mat_index != -1) {
            mesh.instance.material_id = mat.dedicated_material_index == -1
                                            ? mat_index
                                            : world.resources.materials.size() + mat.dedicated_material_index;
        }

        auto mesh_index = world.load_mesh(mesh.id);
        if (mesh_index != -1) {
            mesh.instance.mesh_id = mesh_index;
        }

        auto skeleton_index  = world.load_skeleton(a.skeleton_id);
        auto animation_index = world.load_animation(a.animation_id);

        if (skeleton_index == -1 || animation_index == -1)
            continue;

        mesh.instance.skeleton_id             = skeleton_index;
        mesh.instance.animation_id            = animation_index;
        mesh.instance.animation_output_offset = output_offset;

        output_offset += world.resources.meshes[mesh_index].vertex_count;

        world.renderer.skinned_mesh_count++;
        mesh_instances.push_back(mesh.instance);
        mesh_instance_entities.push_back(e);
    }
}

int main(int argc, char* argv[]) {
    ArgParser args;
    args.parse(argc, argv);

    bool meshlets_requested    = args.get_arg<bool>("meshlets", true);
    bool hardware_rt_requested = args.get_arg<bool>("hardware-rt", true);
    bool vsync_requested       = args.get_arg<bool>("vsync", false);
    bool debug                 = args.get_arg<bool>("debug", false);

    int window_width  = args.get_arg("w", 1920);
    int window_height = args.get_arg("h", 1080);

    std::string project_path = args.get_arg<std::string>("project", "");

    spdlog::set_level(debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("Starting ember");

    spdlog::info("Initializing SDL");
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO)) {
        spdlog::error("Failed to initialize SDL");
        return 1;
    }

    auto* window = SDL_CreateWindow("Ember", window_width, window_height, SDL_WINDOW_VULKAN);
    if (!window) {
        spdlog::error("Failed to create SDL window");
        return 1;
    }

    Editor editor;

    ComponentRegistry::register_components(&editor);

    World world;
    world.initialize(window, meshlets_requested, hardware_rt_requested, vsync_requested);

    world.asset_registry.load(project_path);
    world.script.load_scripts();

    editor.initialize(&world);

    SceneSerializer::load(world.asset_registry.root_path(), world);
    update_transform_hierarchy(world);
    {
        auto view = world.scene.entity_registry.view<components::Transform, components::Mesh, components::Physics>();
        for (auto [e, t, m, p] : view.each()) {
            if (p.is_static == false) {
                continue;
            }

            JPH::TriangleList triangles;
            if (!world.load_collision_mesh(m.id, triangles)) {
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

            JPH::BodyInterface& body_interface = world.physics.system.GetBodyInterface();

            auto                      pos = t.world_position;
            auto                      rot = t.world_rotation;
            JPH::BodyCreationSettings body_settings(
                final_shape,
                JPH::RVec3(pos.x, pos.y, pos.z),
                JPH::Quat(rot.x, rot.y, rot.z, rot.w),
                JPH::EMotionType::Static,
                Layers::NON_MOVING
            );

            JPH::BodyID body_id = body_interface.CreateAndAddBody(body_settings, JPH::EActivation::DontActivate);
            p.body_id           = body_id;
            p.last_scale        = scale;
        }
    }
    world.renderer.wait_idle();

    Buffer pick_buffer = create_buffer(
        sizeof(uint32_t) * 4,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        world.renderer.vma_allocator,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    );

    fill_drawcalls(world, world.renderer.mesh_instances, world.renderer.mesh_instance_entities);

    SceneHistory scene_history;

    bool    use_editor_camera = true;
    Camera* camera;
    Camera  editor_camera = {
         .near_plane      = 0.01f,
         .far_plane       = 1000.0f,
         .viewport_width  = static_cast<float>(world.renderer.swapchain.width),
         .viewport_height = static_cast<float>(world.renderer.swapchain.height),
         .fov             = 90.0f,
         .orientation     = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    editor_camera.position = glm::vec3(0, 0, 0);
    editor_camera.orientation *= glm::angleAxis(glm::radians(-90.0f), glm::vec3(0, 1, 0));

    Camera gameplay_camera;

    camera = &editor_camera;

    float base_camera_speed        = 10.0f;
    float camera_mouse_sensitivity = 0.003f;
    float camera_speed_mod         = 2.5f;
    float camera_speed             = base_camera_speed;

    bool                capturing_mouse   = false;
    ImGuizmo::OPERATION tranform_gizmo_op = ImGuizmo::OPERATION::TRANSLATE;
    bool                using_gizmo       = false;

    bool      enable_transform_snap = false;
    glm::vec3 transform_snap        = glm::vec3(1.0f);
    glm::vec2 grab_origin           = {};

    bool editor_mode = true;
    // To allow being in gameplay mode with the editor overlay
    bool                                        editor_overlay = true;
    std::map<std::string, EditorViewportSource> editor_viewport_source_handles;

    auto add_viewport_source = [&](const std::string& name, Image& image) {
        editor_viewport_source_handles.insert(
            {name, {image, imgui_image_handle(image, world.renderer.linear_sampler)}}
        );
    };

    bool simulate_lower_fps = false;
    int  simulated_fps      = 60;

    add_viewport_source("Anti-Aliased Composite", world.renderer.smaa_output);
    add_viewport_source("Composite", world.renderer.composite_output);
    add_viewport_source("GBuffer Albedo", world.renderer.gbuffer_albedo);
    add_viewport_source("GBuffer Normals", world.renderer.gbuffer_normals);
    add_viewport_source("GBuffer Emissive", world.renderer.gbuffer_emissive);
    add_viewport_source("GBuffer Depth", world.renderer.depth_buffer);
    add_viewport_source("Lighting", world.renderer.lightpass_output);
    add_viewport_source("DDGI Irradiance", world.renderer.ddgi_irradiance);
    add_viewport_source("DDGI Depth", world.renderer.ddgi_depth_atlas);
    add_viewport_source("SMAA Edges", world.renderer.smaa_edges);
    add_viewport_source("RT Reflection", world.renderer.rt_reflection_buffer);
    add_viewport_source("RT Shadows", world.renderer.directional_shadow_buffer);
    add_viewport_source("Bloom Buffer", world.renderer.bloom_buffer);
    add_viewport_source("Composite UI Buffer", world.renderer.ui_buffer);

    std::string editor_viewport_source = "Composite UI Buffer";

    glm::vec4 viewport_pos_size = glm::vec4();
    int       pick_frame        = UINT32_MAX;

    auto  frame_timestamp          = std::chrono::high_resolution_clock::now();
    float delta_time               = 0.0f;
    float time_passed              = 0.0f;
    float total_time               = 0.0;
    float physics_time_accumulator = 0.0f;

    uint32_t accumulated_fps = 0;
    uint32_t fps             = 0;

    auto screen_pos_to_scene_vewport = [&](glm::vec2 pos) -> glm::vec2 {
        return {pos.x - viewport_pos_size.x, pos.y - viewport_pos_size.y};
    };

    auto coords_in_scene_viewport = [&](glm::vec2 pos) -> bool {
        auto coords = screen_pos_to_scene_vewport(pos);

        return coords.x >= 0.0 && coords.x <= viewport_pos_size.z && coords.y >= 0.0 && coords.y <= viewport_pos_size.w;
    };

    scene_history.create_snapshot(world);

    bool running = true;
    while (running) {
        FrameMark;

        bool ui_wants_input = ImGui::GetIO().WantTextInput;

        if (simulate_lower_fps) {
            auto time       = std::chrono::high_resolution_clock::now();
            auto delta_time = std::chrono::duration<float>(time - frame_timestamp).count();

            while (delta_time < 1.0f / simulated_fps) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(1ms);

                time       = std::chrono::high_resolution_clock::now();
                delta_time = std::chrono::duration<float>(time - frame_timestamp).count();
            }
        }

        world.input.mouse_delta             = world.input.mouse_delta_accumulator;
        world.input.mouse_delta_accumulator = glm::vec2(0, 0);

        auto time       = std::chrono::high_resolution_clock::now();
        auto delta_time = std::chrono::duration<float>(time - frame_timestamp).count();
        frame_timestamp = time;

        accumulated_fps++;
        time_passed += delta_time;
        world.time += delta_time;
        physics_time_accumulator += delta_time;

        if (time_passed >= 1.0f) {
            fps = accumulated_fps;

            accumulated_fps = 0;
            time_passed -= 1.0f;
        }

        {
            if (!editor_mode || !use_editor_camera) {
                auto view = world.scene.entity_registry.view<components::Camera, components::Transform>();
                for (auto [e, c, t] : view.each()) {
                    if (c.is_active) {
                        gameplay_camera.near_plane = c.near_plane;
                        gameplay_camera.far_plane  = c.far_plane;

                        gameplay_camera.viewport_width  = c.viewport_width * world.renderer.swapchain.width;
                        gameplay_camera.viewport_height = c.viewport_height * world.renderer.swapchain.height;

                        gameplay_camera.fov        = c.fov;
                        gameplay_camera.ortho_size = c.ortho_size;

                        gameplay_camera.type        = c.type;
                        gameplay_camera.position    = t.world_position;
                        gameplay_camera.orientation = t.world_rotation;
                        update_camera(gameplay_camera);

                        camera = &gameplay_camera;
                        break;
                    }
                }
            } else {
                camera = &editor_camera;
            }
        }

        static std::queue<std::string> dropped_filenames;
        SDL_Event                      window_event;
        while (SDL_PollEvent(&window_event)) {
            ImGui_ImplSDL3_ProcessEvent(&window_event);
            switch (window_event.type) {
            case SDL_EVENT_DROP_BEGIN:
                break;
            case SDL_EVENT_DROP_FILE:
                if (window_event.drop.data != nullptr) {
                    dropped_filenames.push(window_event.drop.data);
                }
                break;
            case SDL_EVENT_DROP_COMPLETE:
                break;
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                world.input.register_key_press(window_event.key.scancode);
                break;
            case SDL_EVENT_KEY_UP:
                world.input.register_key_release(window_event.key.scancode);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                world.input.register_button_press(window_event.button.button);

                if (window_event.button.button == SDL_BUTTON_RIGHT &&
                    coords_in_scene_viewport(world.input.get_mouse_position())) {
                    SDL_SetWindowMouseGrab(window, true);
                    SDL_SetWindowRelativeMouseMode(window, true);

                    capturing_mouse = true;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                world.input.register_button_release(window_event.button.button);

                if (window_event.button.button == SDL_BUTTON_RIGHT && capturing_mouse) {
                    SDL_SetWindowMouseGrab(window, false);
                    SDL_SetWindowRelativeMouseMode(window, false);

                    capturing_mouse = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                auto xrel = static_cast<float>(window_event.motion.xrel);
                auto yrel = static_cast<float>(window_event.motion.yrel);

                auto x = static_cast<float>(window_event.motion.x);
                auto y = static_cast<float>(window_event.motion.y);

                world.input.mouse_pos = {x, y};
                world.input.mouse_delta_accumulator += glm::vec2(xrel, yrel);

                if (capturing_mouse && (use_editor_camera && editor_mode)) {
                    camera->orientation =
                        glm::rotate(
                            glm::quat(0, 0, 0, 1), float(-xrel * camera_mouse_sensitivity), glm::vec3(0, 1, 0)
                        ) *
                        camera->orientation;
                    camera->orientation = glm::rotate(
                                              glm::quat(0, 0, 0, 1),
                                              float(-yrel * camera_mouse_sensitivity),
                                              camera->orientation * glm::vec3(1, 0, 0)
                                          ) *
                                          camera->orientation;
                }
                break;
            }
        }

        glm::vec2 mouse_pos = world.input.get_mouse_position();
        if (editor_mode && !ui_wants_input) {
            if (world.input.is_key_pressed(Key::LEFT_SHIFT) || world.input.is_key_pressed(Key::LEFT_CTRL)) {
                if (world.input.is_key_pressed(Key::LEFT_CTRL)) {
                    camera_speed = base_camera_speed / camera_speed_mod;
                }

                if (world.input.is_key_pressed(Key::LEFT_SHIFT)) {
                    camera_speed = base_camera_speed * camera_speed_mod;
                }
            } else {
                camera_speed = base_camera_speed;
            }

            glm::vec3 velocity = glm::vec3(0.0);
            if (world.input.is_key_pressed(Key::W)) {
                velocity.x = 1;
            }

            if (world.input.is_key_pressed(Key::S)) {
                velocity.x = -1;
            }

            if (world.input.is_key_pressed(Key::A)) {
                velocity.y = -1;
            }

            if (world.input.is_key_pressed(Key::D)) {
                velocity.y = 1;
            }

            if (world.input.is_key_just_pressed(Key::P)) {
                world.renderer.visualize_probes = !world.renderer.visualize_probes;
            }

            if (world.input.is_key_just_pressed(Key::ESCAPE) && world.input.is_key_pressed(Key::LEFT_SHIFT)) {
                running = false;
            }

            if (capturing_mouse && (use_editor_camera && editor_mode)) {
                move_camera(*camera, velocity, camera_speed * delta_time);
            }

            if (world.input.is_key_pressed(Key::LEFT_SHIFT)) {
                if (world.input.is_key_just_pressed(Key::C)) {
                    tranform_gizmo_op = ImGuizmo::OPERATION::SCALEU;
                }

                if (world.input.is_key_just_pressed(Key::R)) {
                    tranform_gizmo_op = ImGuizmo::OPERATION::ROTATE;
                }

                if (world.input.is_key_just_pressed(Key::T)) {
                    tranform_gizmo_op = ImGuizmo::OPERATION::TRANSLATE;
                }
            }

            if (editor.get_selected_entity() != entt::null && world.input.is_key_pressed(Key::LEFT_SHIFT) &&
                world.input.is_key_just_pressed(Key::D) && !capturing_mouse) {
                Entity clone = world.scene.clone_node(editor.get_selected_entity());
                if (clone != entt::null) {
                    editor.set_selected_entity(clone);
                }
                scene_history.create_snapshot(world);
            }

            if (editor.get_selected_entity() != entt::null && world.input.is_key_pressed(Key::DELETE)) {
                if (editor.handle_delete()) {
                    world.scene.delete_node(editor.get_selected_entity());
                    editor.set_selected_entity(entt::null);
                    scene_history.create_snapshot(world);
                }
            }

            if (world.input.is_key_pressed(Key::LEFT_CTRL) && world.input.is_key_just_pressed(Key::Z)) {
                if (!scene_history.undo(world)) {
                    spdlog::debug("Cannot undo, at oldest change");
                }
            }

            if (world.input.is_key_pressed(Key::LEFT_CTRL) && world.input.is_key_just_pressed(Key::R)) {
                if (!scene_history.redo(world)) {
                    spdlog::debug("Cannot redo, at newest change");
                }
            }
        }

        if (world.input.is_key_just_pressed(Key::GRAVE)) {
            editor_overlay = !editor_overlay;
        }

        if (world.input.is_key_just_pressed(Key::F5)) {
            editor_mode = !editor_mode;

            if (editor_mode) {
                // Entering editor state
                editor_overlay   = true;
                world.is_running = false;

                auto sound_view = world.scene.entity_registry.view<components::Sound>();
                for (auto [e, s] : sound_view.each()) {
                    s.instance_id = SoundSystem::INVALID_SOUND_INSTANCE;
                }
                world.sound.stop_all_sounds();

                auto physics_view = world.scene.entity_registry.view<components::Physics>();
                for (auto [e, p] : physics_view.each()) {
                    if (!p.body_id.IsInvalid()) {
                        if (!p.is_static) {
                            world.physics.system.GetBodyInterface().RemoveBody(p.body_id);
                            world.physics.system.GetBodyInterface().DestroyBody(p.body_id);
                        }
                    }
                }

                auto controller_view = world.scene.entity_registry.view<components::CharacterController>();
                for (auto [e, c] : controller_view.each()) {
                    delete c.controller;
                }

                world.script.clear();
                world.resources.runtime_materials.clear();
                scene_history.load_snapshot(world);

                SDL_SetWindowMouseGrab(window, false);
                SDL_SetWindowRelativeMouseMode(window, false);
                capturing_mouse = false;
            } else {
                // Entering play state
                editor_overlay = false;

                if (scene_history.snapshot_count() == 0) {
                    scene_history.create_snapshot(world);
                }
                world.is_running = true;
                world.time       = 0.0f;

                auto controller_view =
                    world.scene.entity_registry.view<components::CharacterController, components::Transform>();
                for (auto [e, c, t] : controller_view.each()) {
                    JPH::Ref<JPH::CharacterVirtualSettings> character_settings = new JPH::CharacterVirtualSettings();
                    character_settings->mShape = new JPH::CapsuleShape(c.height / 2.0f - c.radius, c.radius);

                    c.controller = new JPH::CharacterVirtual(
                        character_settings,
                        JPH::RVec3(t.world_position.x, t.world_position.y, t.world_position.z),
                        JPH::Quat(t.world_rotation.x, t.world_rotation.y, t.world_rotation.z, t.world_rotation.w),
                        0,
                        &world.physics.system
                    );
                    c.controller->SetEnhancedInternalEdgeRemoval(c.enhanced_edge_removal);
                }

                auto sound_view = world.scene.entity_registry.view<components::Sound>();
                for (auto [e, s] : sound_view.each()) {
                    if (s.autoplay && s.instance_id == SoundSystem::INVALID_SOUND_INSTANCE) {
                        s.instance_id = world.node_play_sound(e);
                    }
                }

                auto view = world.scene.entity_registry.view<components::Script>();
                for (auto [e, s] : view.each()) {
                    world.script.construct_script_objects(e, s);
                }

                for (auto [e, s] : view.each()) {
                    world.script.call_on_start(s);
                }
            }
        }

        if (!editor_overlay) {
            SDL_SetWindowMouseGrab(window, true);
            SDL_SetWindowRelativeMouseMode(window, true);
            capturing_mouse = true;
        }

        {
            auto view = world.scene.entity_registry.view<components::Transform, components::Sound>();
            for (auto [entity, t, s] : view.each()) {
                world.sound.set_sound_properties(
                    s.instance_id,
                    s.volume,
                    s.pitch,
                    s.min_distance,
                    s.max_distance,
                    s.rolloff,
                    s.loop,
                    t.world_position
                );
            }

            world.sound.update();
        }

        {
            ZoneScopedN("Physics Sync Bodies");
            auto view = world.scene.entity_registry.view<components::Transform, components::Physics>();
            for (auto [entity, transform, physics] : view.each()) {
                if (physics.body_id.IsInvalid()) {
                    continue;
                }

                JPH::EActivation activation =
                    physics.is_static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate;

                if (transform.world_scale != physics.last_scale && transform.world_scale != 0.0f) {
                    float scale_delta = transform.world_scale / physics.last_scale;
                    auto  shape       = world.physics.system.GetBodyInterface().GetShape(physics.body_id);
                    auto  new_shape   = shape->ScaleShape(JPH::Vec3(scale_delta, scale_delta, scale_delta));
                    if (new_shape.IsValid()) {
                        world.physics.system.GetBodyInterface().SetShape(
                            physics.body_id, new_shape.Get(), false, activation
                        );
                    }

                    physics.last_scale = transform.world_scale;
                }

                if (!physics.is_static && !transform.dirty) {
                    continue;
                }

                world.physics.system.GetBodyInterface().SetPosition(
                    physics.body_id,
                    JPH::Vec3(transform.world_position.x, transform.world_position.y, transform.world_position.z),
                    activation
                );

                world.physics.system.GetBodyInterface().SetRotation(
                    physics.body_id,
                    JPH::Quat(
                        transform.world_rotation.x,
                        transform.world_rotation.y,
                        transform.world_rotation.z,
                        transform.world_rotation.w
                    ),
                    activation
                );

                if (!physics.is_static && transform.dirty) {
                    transform.dirty = false;
                }
            }
        }

        {
            ZoneScopedN("Physics Update");
            auto update_view = world.scene.entity_registry.view<components::Transform, components::Physics>();

            while (physics_time_accumulator >= world.physics.frame_time) {
                if (!editor_mode) {
                    auto script_view = world.scene.entity_registry.view<components::Script>();
                    for (auto [e, s] : script_view.each()) {
                        world.script.call_on_fixed_update(s, world.physics.frame_time);
                    }
                }

                for (auto [entity, transform, physics] : update_view.each()) {
                    if (physics.body_id.IsInvalid()) {
                        continue;
                    }

                    JPH::Vec3 p;
                    JPH::Quat r;
                    world.physics.system.GetBodyInterface().GetPositionAndRotation(physics.body_id, p, r);

                    physics.last_position = p;
                    physics.last_rotation = r;
                }

                world.physics.update();
                physics_time_accumulator -= world.physics.frame_time;
            }

            {
                ZoneScopedN("Interpolate Physics Objects");
                auto interpolate_view =
                    world.scene.entity_registry.view<components::Transform, components::Physics, components::Mesh>();
                float physics_alpha = physics_time_accumulator / world.physics.frame_time;
                for (auto [entity, transform, physics, m] : interpolate_view.each()) {
                    if (physics.is_static || physics.body_id.IsInvalid()) {
                        continue;
                    }

                    JPH::Vec3 p;
                    JPH::Quat r;
                    world.physics.system.GetBodyInterface().GetPositionAndRotation(physics.body_id, p, r);

                    glm::vec3 new_pos = glm::vec3(p.GetX(), p.GetY(), p.GetZ());
                    glm::quat new_rot = glm::quat(r.GetX(), r.GetY(), r.GetZ(), r.GetW());

                    glm::vec3 old_pos = glm::vec3(
                        physics.last_position.GetX(), physics.last_position.GetY(), physics.last_position.GetZ()
                    );
                    glm::quat old_rot = glm::quat(
                        physics.last_rotation.GetX(),
                        physics.last_rotation.GetY(),
                        physics.last_rotation.GetZ(),
                        physics.last_rotation.GetW()
                    );

                    int mesh_index = m.instance.mesh_id;
                    if (mesh_index != -1) {
                        Mesh& geometry = world.resources.meshes[mesh_index];

                        glm::vec3 center = (geometry.bounds_max + geometry.bounds_min) * 0.5f;
                        glm::vec3 offset = new_rot * (center * m.instance.scale);

                        transform.position = glm::mix(old_pos, new_pos, physics_alpha) - offset;
                        transform.rotation = glm::slerp(old_rot, new_rot, physics_alpha);
                    }
                }
            }
        }

        if (!editor_mode) {
            auto script_view = world.scene.entity_registry.view<components::Script>();
            for (auto [e, s] : script_view.each()) {
                world.script.call_on_update(s, delta_time);
            }
        }

        {
            ZoneScopedN("Update Character Controllers");
            auto view = world.scene.entity_registry.view<components::CharacterController, components::Transform>();

            for (auto [e, c, t] : view.each()) {
                if (!c.controller) {
                    continue;
                }

                JPH::CharacterVirtual::EGroundState ground_state = c.controller->GetGroundState();
                c.is_grounded = (ground_state == JPH::CharacterVirtual::EGroundState::OnGround);

                JPH::Vec3 jolt_normal = c.controller->GetGroundNormal();
                c.ground_normal       = glm::vec3(jolt_normal.GetX(), jolt_normal.GetY(), jolt_normal.GetZ());

                if (!c.is_grounded) {
                    JPH::Vec3 gravity = world.physics.system.GetGravity();
                    c.velocity.y      = c.velocity.y + gravity.GetY() * delta_time;
                }

                c.controller->SetLinearVelocity(JPH::Vec3(c.velocity.x, c.velocity.y, c.velocity.z));

                JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
                update_settings.mStickToFloorStepDown = JPH::Vec3(0, -c.step_down_distance, 0);
                update_settings.mWalkStairsStepUp     = JPH::Vec3(0, c.step_up_height, 0);

                c.controller->ExtendedUpdate(
                    delta_time,
                    world.physics.system.GetGravity(),
                    update_settings,
                    world.physics.system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                    world.physics.system.GetDefaultLayerFilter(Layers::MOVING),
                    {},
                    {},
                    *world.physics.temp_allocator
                );

                JPH::RVec3 new_pos = c.controller->GetPosition();
                t.position         = glm::vec3(new_pos.GetX(), new_pos.GetY(), new_pos.GetZ());

                JPH::Vec3 new_vel = c.controller->GetLinearVelocity();
                c.velocity        = glm::vec3(new_vel.GetX(), new_vel.GetY(), new_vel.GetZ());
            }
        }

        update_transform_hierarchy(world);

        if (world.renderer.frame_count == pick_frame) {
            void* ptr;
            VK_CHECK(vmaMapMemory(world.renderer.vma_allocator, pick_buffer.allocation, &ptr));
            uint32_t mesh_id = *reinterpret_cast<uint32_t*>(ptr);

            if (mesh_id == UINT32_MAX || mesh_id >= world.renderer.mesh_instance_entities.size()) {
                editor.set_selected_entity(entt::null);
            } else {
                editor.set_selected_entity(world.renderer.mesh_instance_entities[mesh_id]);
            }

            vmaUnmapMemory(world.renderer.vma_allocator, pick_buffer.allocation);
            pick_frame = UINT32_MAX;
        }

        update_camera(*camera);

        fill_drawcalls(world, world.renderer.mesh_instances, world.renderer.mesh_instance_entities);
        world.renderer.editor_overlay = editor_overlay;
        world.renderer.begin_frame(camera);

        ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        if (editor_overlay) {
            if (editor.render_main_menu()) {
                scene_history.create_snapshot(world);
            };

            ImGui::Begin(ICON_FA_VIDEO " Scene Viewport", nullptr, ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu(ICON_FA_ARROW_CIRCLE_RIGHT " Viewport Source")) {
                    for (auto [name, handle] : editor_viewport_source_handles) {
                        if (ImGui::MenuItem(name.c_str(), nullptr, editor_viewport_source.compare(name) == 0)) {
                            editor_viewport_source = name;
                        }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Text(": %s", editor_viewport_source.c_str());
                ImGui::Text("        State: %s", editor_mode ? "Editor" : "Gameplay");
                ImGui::Text("        Use Editor Camera: ");
                ImGui::Checkbox("##use_editor_camera", &use_editor_camera);
                ImGui::EndMenuBar();
            }

            auto region = ImGui::GetContentRegionAvail();
            auto cursor = ImGui::GetCursorScreenPos();

            if (editor_viewport_source_handles.contains(editor_viewport_source)) {
                auto source = editor_viewport_source_handles[editor_viewport_source];

                float aspect_ratio = (float)source.image.width / (float)source.image.height;

                ImVec2 size = ImVec2(region.y * aspect_ratio, region.y);
                if (size.x > region.x) {
                    size = ImVec2(region.x, region.x / aspect_ratio);
                }

                viewport_pos_size = glm::vec4(cursor.x, cursor.y, size.x, size.y);

                ImGui::Image(source.descriptor_set, size);
                ImGui::End();
            }

        } else {
            viewport_pos_size = glm::vec4(0, 0, world.renderer.swapchain.width, world.renderer.swapchain.height);
        }

        if (editor.render_scene_node_property_window()) {
            scene_history.create_snapshot(world);
        }

        if (dropped_filenames.size() > 0) {
            editor.on_files_dropped(dropped_filenames);

            while (!dropped_filenames.empty()) {
                dropped_filenames.pop();
            }
        }
        editor.render_asset_importer();
        editor.render_asset_explorer();

        std::vector<std::pair<std::string, PassTiming>> pass_timings = {};
        for (const auto& pass : world.renderer.framegraph->passes) {
            pass_timings.push_back(std::make_pair(pass.name, world.renderer.framegraph->get_pass_timing(pass.name)));
        }
        if (editor.render_performance_window(pass_timings)) {
            scene_history.create_snapshot(world);
        }

        ImGui::Begin(ICON_FA_COGS " Configuration");
        ImGui::InputInt("Simulate Target FPS", &simulated_fps);
        ImGui::Checkbox("Simulate FPS", &simulate_lower_fps);
        if (ImGui::CollapsingHeader("Renderer Info", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Rendering path: %s", world.renderer.meshlets_enabled ? "Meshlets" : "Indirect");
            ImGui::Text("Raytracing enabled: %s", world.renderer.hardware_rt_enabled ? "Yes" : "No");
            ImGui::Text("FPS: %u", fps);
            ImGui::NewLine();

            ImGui::Text("Meshes In Scene: %lu", world.renderer.mesh_instances.size());
            ImGui::Text("Static Meshes: %u", world.renderer.static_mesh_count);
            ImGui::Text("Skinned Meshes: %u", world.renderer.skinned_mesh_count);

            ImGui::NewLine();
            ImGui::Text("Unique Materials: %lu", world.resources.materials.size());
            ImGui::Text("Material Overrides: %lu", world.resources.runtime_materials.size());
            ImGui::Text("Textures: %lu", world.resources.images.size());
            ImGui::Text("Samplers: %lu", world.resources.samplers.size());
            ImGui::Text("Meshes: %lu", world.resources.meshes.size());
            ImGui::NewLine();

            auto to_mb = [](uint64_t bytes) {
                return (double)bytes / 1024.0 / 1024.0;
            };

            ImGui::Text(
                "Vertex Buffer Usage: %.3fMB / %3.fMB",
                to_mb(world.renderer.buffer_offsets.vertex_buffer),
                to_mb(world.renderer.buffers.vertex_buffer.size)
            );
            ImGui::Text(
                "Index Buffer Usage: %.3fMB / %3.fMB",
                to_mb(world.renderer.buffer_offsets.index_buffer),
                to_mb(world.renderer.buffers.index_buffer.size)
            );
            ImGui::Text(
                "Meshlet Buffer Usage: %.3fMB / %3.fMB",
                to_mb(world.renderer.buffer_offsets.meshlet_buffer),
                to_mb(world.renderer.buffers.meshlet_buffer.size)
            );
            ImGui::Text(
                "Meshlet Indices Buffer Usage: %.3fMB / %3.fMB",
                to_mb(world.renderer.buffer_offsets.meshlet_vertex_indices),
                to_mb(world.renderer.buffers.meshlet_vertex_indices.size)
            );
            ImGui::Text(
                "Meshlet Primitive Buffer Usage: %.3fMB / %3.fMB",
                to_mb(world.renderer.buffer_offsets.meshlet_primitive_buffer),
                to_mb(world.renderer.buffers.meshlet_primitive_buffer.size)
            );
            ImGui::Text(
                "Meshlet Bounds Buffer Usage: %.3fMB / %3.fMB",
                to_mb(world.renderer.buffer_offsets.meshlet_bounds_buffer),
                to_mb(world.renderer.buffers.meshlet_bounds_buffer.size)
            );
            ImGui::Text(
                "Skin Buffer Usage: %.3fMB / %3.fMB",
                to_mb(world.renderer.buffer_offsets.skin_buffer_offset),
                to_mb(world.renderer.buffers.skin_buffer.size)
            );
            ImGui::NewLine();

            ImGui::Text("Texture Usage: %.3fMB", to_mb(world.renderer.buffer_offsets.texture_data_size));
            ImGui::NewLine();

            ImGui::Text("Triangles Rendered: %.3fM", (double(world.renderer.pipeline_stats[0]) / 1'000'000.0));
            ImGui::Text("Fragment shader invocations: %.3fM", (double(world.renderer.pipeline_stats[1]) / 1'000'000.0));
        }

        if (ImGui::CollapsingHeader("Culling & LOD's")) {
            if (ImGui::Checkbox("Freeze frustum", &world.renderer.debug_frustum)) {
                auto transposed_projection = glm::transpose(camera->projection_matrix);

                glm::vec4 frustum_x = normalize_plane(transposed_projection[3] + transposed_projection[0]);
                glm::vec4 frustum_y = normalize_plane(transposed_projection[3] + transposed_projection[1]);

                world.renderer.frozen_view       = camera->view_matrix;
                world.renderer.frozen_frustum[0] = frustum_x.x;
                world.renderer.frozen_frustum[1] = frustum_x.z;
                world.renderer.frozen_frustum[2] = frustum_y.y;
                world.renderer.frozen_frustum[3] = frustum_y.z;
            }
            ImGui::SameLine();
            ImGui::Checkbox("Enable LOD's", (bool*)&world.renderer.cull_push_constants.enable_lods);
            ImGui::SliderInt("Min LOD Level", &world.renderer.min_lod, 0, 8);
            ImGui::NewLine();

            ImGui::Checkbox("Disable culling (global)", &world.renderer.disable_culling);
            ImGui::Checkbox(
                "Disable frustum cull (compute)", (bool*)&world.renderer.cull_push_constants.disable_frustum_cull
            );
            ImGui::Checkbox(
                "Disable depth cull (compute)", (bool*)&world.renderer.cull_push_constants.disable_depth_cull
            );
            ImGui::Checkbox(
                "Disable frustum cull (mesh)", (bool*)&world.renderer.gpass_push_constants.disable_frustum_cull
            );
            ImGui::Checkbox(
                "Disable depth cull (mesh)", (bool*)&world.renderer.gpass_push_constants.disable_depth_cull
            );
            ImGui::Checkbox("Disable cone cull (mesh)", (bool*)&world.renderer.gpass_push_constants.disable_cone_cull);
            ImGui::Checkbox(
                "Disable small triangle cull (mesh)",
                (bool*)&world.renderer.gpass_push_constants.disable_small_triangle_cull
            );
        }

        if (ImGui::CollapsingHeader("Rendering")) {
            ImGui::SeparatorText("Directional Light");
            ImGui::DragFloat3("Direction", &world.renderer.lighting_data.light_direction.x, 0.01, -1.0, 1.0);
            ImGui::ColorEdit3(
                "Color",
                &world.renderer.lighting_data.light_color.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );
            ImGui::SliderFloat("Intensity", &world.renderer.lighting_data.light_color.w, 0.0, 100.0);

            ImGui::SeparatorText("Sky");
            ImGui::ColorEdit3(
                "Top Hemisphere",
                &world.renderer.lighting_data.sky_hemisphere_top.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );
            ImGui::ColorEdit3(
                "Bottom Hemisphere",
                &world.renderer.lighting_data.sky_hemisphere_bottom.x,
                ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float
            );

            ImGui::SeparatorText("DDGI");
            ImGui::DragFloat("GI Intensity", &world.renderer.lighting_data.gi_intensity, 0.01);
            ImGui::DragFloat("Probe Spacing", &world.renderer.lighting_data.probe_spacing, 0.01, 0.1, 10.0);
            ImGui::DragFloat3("Grid Origin", &world.renderer.lighting_data.grid_origin.x, 0.03, -100.0, 100.0);
            ImGui::Checkbox("Visualize Probes", (bool*)&world.renderer.visualize_probes);
            ImGui::Checkbox(
                "Cull Innactive Probes", (bool*)&world.renderer.debug_renderer_constants.cull_innactive_probes
            );
            ImGui::Checkbox("Multibounce Diffuse", (bool*)&world.renderer.lighting_data.multibounce);

            ImGui::SeparatorText("Light Pass");
            ImGui::Checkbox("Use Bent Normals", (bool*)&world.renderer.lighting_data.use_bent_normals);
            ImGui::Checkbox("Remove Visibility Checks", (bool*)&world.renderer.lighting_data.remove_visibility_checks);
            ImGui::Checkbox("Compensate Specular", (bool*)&world.renderer.lighting_data.compensate_specular);
            ImGui::Checkbox("Disney Diffuse", (bool*)&world.renderer.lighting_data.disney_diffuse);

            ImGui::SeparatorText("Bloom");
            ImGui::SliderFloat("Bloom Upscale radius", &world.renderer.bloom_upscale_sample_scale, 0.0, 5.0);
            ImGui::SliderFloat("Bloom Strength", &world.renderer.composite_push_constants.bloom_strength, 0.0, 1.0);
            ImGui::SliderInt("Bloom Levels", &world.renderer.bloom_levels, 0, world.renderer.bloom_buffer.levels);

            ImGui::SeparatorText("Post Process");
            ImGui::Checkbox("Use GT5 tonemapping", (bool*)&world.renderer.composite_push_constants.tonemapping_type);
            ImGui::Checkbox(
                "Enable Auto Exposure", (bool*)&world.renderer.composite_push_constants.enable_auto_exposure
            );
            ImGui::SliderFloat(
                "EV Compensation",
                &world.renderer.composite_push_constants.exposure_compensation,
                -3.0f,
                3.0f,
                "%.2f EV"
            );

            if (!world.renderer.composite_push_constants.enable_auto_exposure) {
                ImGui::Separator();
                ImGui::Text("Manual Exposure Settings");

                if (ImGui::SliderFloat("Aperture", &world.renderer.camera_aperture, 1.0f, 22.0f, "f/%.1f")) {
                    const float f_stops[] = {1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f};
                    float       closest   = f_stops[0];
                    float       min_dist  = fabsf(world.renderer.camera_aperture - closest);
                    for (float stop : f_stops) {
                        float dist = fabsf(world.renderer.camera_aperture - stop);
                        if (dist < min_dist) {
                            min_dist = dist;
                            closest  = stop;
                        }
                    }
                    if (min_dist < 0.2f) {
                        world.renderer.camera_aperture = closest;
                    }
                }

                float shutter_log = -log2f(world.renderer.camera_shutter_time);
                if (ImGui::SliderFloat(
                        "Shutter Speed", &shutter_log, 0.0f, 13.0f, "1/%.0f", ImGuiSliderFlags_Logarithmic
                    )) {
                    world.renderer.camera_shutter_time = powf(2.0f, -shutter_log);
                }

                ImGui::SliderFloat(
                    "ISO", &world.renderer.camera_iso, 100.0f, 6400.0f, "%.0f", ImGuiSliderFlags_Logarithmic
                );
            } else {
                ImGui::SliderFloat("Min Log Luminance", &world.renderer.min_log_lum, -16.0f, 0.0f, "%.1f");
                ImGui::SliderFloat("Max Log Luminance", &world.renderer.max_log_lum, 1.0f, 8.0f, "%.1f");

                ImGui::SliderFloat(
                    "Min EV100", &world.renderer.composite_push_constants.min_ev100, -10.0f, 10.0f, "%.1f"
                );
                ImGui::SliderFloat(
                    "Max EV100", &world.renderer.composite_push_constants.max_ev100, -5.0f, 20.0f, "%.1f"
                );
                ImGui::SliderFloat("Adaptation Speed", &world.renderer.adaption_speed, 0.1f, 10.0f, "%.1f");
            }
        }

        if (ImGui::CollapsingHeader("Transform Gizmo")) {
            ImGui::Checkbox("Enable Transform Snap", &enable_transform_snap);

            if (ImGui::InputFloat("Transform Snap", &transform_snap.x, 1.0f)) {
                transform_snap = glm::vec3(transform_snap.x);
            }
        }
        ImGui::End();

        if (editor.render_scene_hierarchy_window()) {
            scene_history.create_snapshot(world);
        }

        editor.render_particle_editor();

        if (editor.get_selected_entity() != entt::null) {
            auto t = world.scene.get_component<components::Transform>(editor.get_selected_entity());
            auto p = world.scene.get_component<components::Parent>(editor.get_selected_entity());

            glm::mat4 transform = glm::translate(glm::mat4(1.0f), t->world_position);
            transform           = transform * glm::mat4_cast(t->world_rotation);
            transform           = glm::scale(transform, glm::vec3(t->world_scale));

            auto      angle      = glm::normalize(glm::eulerAngles(camera->orientation));
            glm::mat4 view       = glm::mat4_cast(camera->orientation);
            glm::mat4 projection = glm::perspective(
                glm::radians(camera->fov), camera->viewport_width / camera->viewport_height, 0.01f, 1000.0f
            );

            view = camera->view_matrix;

            view = glm::scale(glm::identity<glm::mat4>(), glm::vec3(1, 1, -1)) * view;

            glm::mat4 delta_mat;

            ImGuizmo::SetRect(viewport_pos_size.x, viewport_pos_size.y, viewport_pos_size.z, viewport_pos_size.w);
            ImGuizmo::SetAlternativeWindow(ImGui::FindWindowByName(ICON_FA_VIDEO " Scene Viewport"));
            if (ImGuizmo::Manipulate(
                    &view[0].x,
                    &projection[0].x,
                    tranform_gizmo_op,
                    ImGuizmo::MODE::WORLD,
                    &transform[0].x,
                    &delta_mat[0].x,
                    enable_transform_snap ? &transform_snap.x : nullptr
                )) {
                using_gizmo = true;

                glm::vec3 position;
                glm::vec3 rotation;
                glm::vec3 scale;
                ImGuizmo::DecomposeMatrixToComponents(&delta_mat[0].x, &position.x, &rotation.x, &scale.x);

                if (tranform_gizmo_op == ImGuizmo::OPERATION::TRANSLATE) {
                    t->position +=
                        position * (p ? world.scene.get_component<components::Transform>(p->parent)->world_rotation
                                      : glm::quat(0, 0, 0, 1));
                }

                if (tranform_gizmo_op == ImGuizmo::OPERATION::ROTATE) {
                    glm::quat parent_world_rot =
                        p ? world.scene.get_component<components::Transform>(p->parent)->world_rotation
                          : glm::quat(0, 0, 0, 1);
                    glm::quat parent_inv = glm::inverse(parent_world_rot);

                    glm::quat delta = glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.x), glm::vec3(1, 0, 0)) *
                                      glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.y), glm::vec3(0, 1, 0)) *
                                      glm::rotate(glm::quat(0, 0, 0, 1), glm::radians(rotation.z), glm::vec3(0, 0, 1));

                    t->rotation = parent_inv * delta * parent_world_rot * t->rotation;
                }

                if (tranform_gizmo_op == ImGuizmo::OPERATION::SCALEU) {
                    t->scale *= scale.x;
                }

                auto p = world.scene.get_component<components::Physics>(editor.get_selected_entity());
                if (p && !p->is_static && !p->body_id.IsInvalid()) {
                    JPH::EActivation activation = JPH::EActivation::Activate;

                    auto last_position = world.physics.system.GetBodyInterface().GetPosition(p->body_id);
                    auto new_position  = JPH::Vec3(
                        last_position.GetX() + position.x,
                        last_position.GetY() + position.y,
                        last_position.GetZ() + position.z
                    );

                    if (tranform_gizmo_op == ImGuizmo::OPERATION::TRANSLATE) {
                        world.physics.system.GetBodyInterface().SetPosition(p->body_id, new_position, activation);
                    }

                    if (tranform_gizmo_op == ImGuizmo::OPERATION::ROTATE) {
                        world.physics.system.GetBodyInterface().SetRotation(
                            p->body_id,
                            JPH::Quat(t->rotation.x, t->rotation.y, t->rotation.z, t->rotation.w),
                            activation
                        );
                    }

                    if (tranform_gizmo_op == ImGuizmo::OPERATION::SCALEU) {
                        auto shape     = world.physics.system.GetBodyInterface().GetShape(p->body_id);
                        auto new_shape = shape->ScaleShape(JPH::Vec3(scale.x, scale.x, scale.x));
                        if (new_shape.IsValid()) {
                            world.physics.system.GetBodyInterface().SetShape(
                                p->body_id, new_shape.Get(), false, activation
                            );
                        }
                    }
                }
            } else {
                if (!world.input.is_button_pressed(Button::LEFT) && using_gizmo) {
                    scene_history.create_snapshot(world);
                    using_gizmo = false;
                }
            }
        }

        world.renderer.render_frame(delta_time);

        if (!capturing_mouse && coords_in_scene_viewport(mouse_pos)) {
            if (world.input.is_button_just_pressed(Button::LEFT) && !using_gizmo &&
                world.input.is_key_pressed(Key::LEFT_SHIFT)) {
                glm::vec2 pos  = screen_pos_to_scene_vewport(mouse_pos);
                glm::vec2 frac = pos / glm::vec2(viewport_pos_size.z, viewport_pos_size.w);

                int mouse_x = glm::floor(frac.x * world.renderer.gbuffer_id.width);
                int mouse_y = glm::floor(frac.y * world.renderer.gbuffer_id.height);

                VkBufferImageCopy region = {
                    .bufferOffset      = 0,
                    .bufferRowLength   = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource =
                        {
                            .aspectMask     = world.renderer.gbuffer_id.aspect,
                            .mipLevel       = 0,
                            .baseArrayLayer = 0,
                            .layerCount     = 1,
                        },
                    .imageOffset =
                        {
                            .x = mouse_x,
                            .y = mouse_y,
                            .z = 0,
                        },
                    .imageExtent = {
                        .width  = 1,
                        .height = 1,
                        .depth  = 1,
                    }
                };

                image_pipeline_barrier(
                    world.renderer.gbuffer_id,
                    world.renderer.get_current_command_buffer(),
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT
                );

                vkCmdCopyImageToBuffer(
                    world.renderer.get_current_command_buffer(),
                    world.renderer.gbuffer_id.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    pick_buffer.handle,
                    1,
                    &region
                );

                image_pipeline_barrier(
                    world.renderer.gbuffer_id,
                    world.renderer.get_current_command_buffer(),
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                );

                pick_frame = world.renderer.frame_count + Renderer::FRAMES_IN_FLIGHT + 1;
            }
        }

        world.renderer.end_frame();

        {
            auto view = world.scene.entity_registry.view<components::Transform, components::Mesh>();
            for (auto [e, t, m] : view.each()) {
                m.instance.last_position = m.instance.position;
                m.instance.last_scale    = m.instance.scale;
                m.instance.last_rotation = m.instance.rotation;
            }
        }

        world.input.update_key_states();
    }
    world.renderer.wait_idle();

    spdlog::info("Cleaning up");

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplVulkan_Shutdown();

    destroy_swapchain(world.renderer.swapchain, window, world.renderer.instance, world.renderer.device);
    destroy_buffer(pick_buffer, world.renderer.device, world.renderer.vma_allocator);
    world.cleanup();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
