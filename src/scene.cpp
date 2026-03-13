#include "scene.hpp"

#include "world.hpp"

#include <tracy/Tracy.hpp>

#include <ktx.h>
#include <map>
#include <thread>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

template <> void Scene::remove_component_internal<components::Physics>(Entity node) {
    auto p = get_component<components::Physics>(node);

    if (!p->body_id.IsInvalid()) {
        world->physics.system.GetBodyInterface().RemoveBody(p->body_id);
    }

    entity_registry.remove<components::Physics>(node);
}

void Scene::initialize(class World* world) {
    this->world = world;
}

Entity Scene::create_node(const std::string& name) {
    Entity e = entity_registry.create();

    entity_registry.emplace<components::Name>(e, name);
    entity_registry.emplace<components::Transform>(e);

    return e;
}

void Scene::delete_node(Entity node, bool delete_children) {
    auto c = get_component<components::Children>(node);
    if (c) {
        auto children_copy = c->children;

        for (auto child : children_copy) {
            if (delete_children) {
                delete_node(child, delete_children);
            } else {
                auto p = get_component<components::Parent>(node);
                if (p) {
                    set_node_parent(child, p->parent);
                } else {
                    remove_node_parent(child);
                }
            }
        }
    }
    remove_node_parent(node);
    remove_all_components(node);

    entity_registry.destroy(node);
}

void Scene::cleanup() {
}

void Scene::set_node_parent(Entity child, Entity parent) {
    if (entity_registry.all_of<components::Parent>(child)) {
        auto old_parent = entity_registry.get<components::Parent>(child).parent;
        if (entity_registry.valid(old_parent) && entity_registry.all_of<components::Children>(old_parent)) {
            auto& children = entity_registry.get<components::Children>(old_parent).children;
            children.erase(std::remove(children.begin(), children.end(), child), children.end());
        }
    }

    entity_registry.emplace_or_replace<components::Parent>(child, parent);

    if (!entity_registry.all_of<components::Children>(parent)) {
        entity_registry.emplace<components::Children>(parent);
    }

    entity_registry.get<components::Children>(parent).children.push_back(child);
}

void Scene::remove_node_parent(Entity child) {
    if (!entity_registry.all_of<components::Parent>(child)) {
        return;
    }

    auto parent = entity_registry.get<components::Parent>(child).parent;
    if (entity_registry.valid(parent) && entity_registry.all_of<components::Children>(parent)) {
        auto& children = entity_registry.get<components::Children>(parent).children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }

    entity_registry.remove<components::Parent>(child);
}

Entity Scene::clone_node(Entity base) {
    return clone_node_internal(base, entt::null);
}

Entity Scene::clone_node_internal(Entity base, Entity cloned_parent) {
    auto src_name = get_component<components::Name>(base);

    std::string new_name = src_name->name;
    if (cloned_parent == entt::null) {
        new_name += "_clone";
    }

    Entity new_entity = create_node(new_name);

    auto src_transform                                = get_component<components::Transform>(base);
    *get_component<components::Transform>(new_entity) = *src_transform;

    auto src_physics = get_component<components::Physics>(base);
    if (src_physics && !src_physics->body_id.IsInvalid()) {
        auto& p              = add_component<components::Physics>(new_entity);
        auto& body_interface = world->physics.system.GetBodyInterface();

        JPH::EMotionType motion_type = body_interface.GetMotionType(src_physics->body_id);
        JPH::Vec3        position    = body_interface.GetPosition(src_physics->body_id);
        JPH::Quat        rotation    = body_interface.GetRotation(src_physics->body_id);

        const JPH::Shape* shape = body_interface.GetShape(src_physics->body_id);

        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(position),
            rotation,
            motion_type,
            motion_type == JPH::EMotionType::Static ? Layers::NON_MOVING : Layers::MOVING
        );

        settings.mFriction      = body_interface.GetFriction(src_physics->body_id);
        settings.mGravityFactor = body_interface.GetGravityFactor(src_physics->body_id);

        JPH::Body*  new_body = body_interface.CreateBody(settings);
        JPH::BodyID new_id   = new_body->GetID();

        body_interface.AddBody(
            new_id,
            motion_type == JPH::EMotionType::Static ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
        );

        p.body_id    = new_id;
        p.is_static  = motion_type == JPH::EMotionType::Static;
        p.last_scale = src_transform->scale;
    }

    auto src_mesh = get_component<components::Mesh>(base);
    if (src_mesh) {
        auto& m    = add_component<components::Mesh>(new_entity);
        m.id       = src_mesh->id;
        m.instance = src_mesh->instance;
    }

    auto src_material = get_component<components::Material>(base);
    if (src_material) {
        auto& m = add_component<components::Material>(new_entity);
        m.id    = src_material->id;
    }

    auto src_children = get_component<components::Children>(base);
    if (src_children) {
        for (Entity child : src_children->children) {
            Entity new_child = clone_node_internal(child, new_entity);
            set_node_parent(new_child, new_entity);
        }
    }

    Entity parent     = cloned_parent == entt::null ? base : cloned_parent;
    auto   src_parent = get_component<components::Parent>(parent);
    if (src_parent) {
        set_node_parent(new_entity, src_parent->parent);
    }

    auto src_script = get_component<components::Script>(base);
    if (src_script) {
        auto& script   = add_component<components::Script>(new_entity);
        script.scripts = src_script->scripts;
    }

    auto src_tag = get_component<components::Tag>(base);
    if (src_tag) {
        auto& tag = add_component<components::Tag>(new_entity);
        tag.tags  = src_tag->tags;
    }

    auto src_camera = get_component<components::Camera>(base);
    if (src_camera) {
        auto& camera           = add_component<components::Camera>(new_entity);
        camera.near_plane      = src_camera->near_plane;
        camera.far_plane       = src_camera->far_plane;
        camera.fov             = src_camera->fov;
        camera.viewport_x      = src_camera->viewport_x;
        camera.viewport_y      = src_camera->viewport_y;
        camera.viewport_width  = src_camera->viewport_width;
        camera.viewport_height = src_camera->viewport_height;
        camera.ortho_size      = src_camera->ortho_size;
        camera.type            = src_camera->type;
        camera.is_active       = src_camera->is_active;
    }

    auto src_sprite = get_component<components::Sprite>(base);
    if (src_sprite) {
        auto& sprite      = add_component<components::Sprite>(new_entity);
        sprite.size       = src_sprite->size;
        sprite.pivot      = src_sprite->pivot;
        sprite.color      = src_sprite->color;
        sprite.uvs        = src_sprite->uvs;
        sprite.texture_id = src_sprite->texture_id;
    }

    auto src_text = get_component<components::Text>(base);
    if (src_text) {
        auto& text   = add_component<components::Text>(new_entity);
        text.text    = src_text->text;
        text.pivot   = src_text->pivot;
        text.color   = src_text->color;
        text.font_id = src_text->font_id;
    }

    if (get_component<components::World>(base)) {
        add_component<components::World>(new_entity);
    }

    auto src_sound = get_component<components::Sound>(base);
    if (src_sound) {
        auto& sound        = add_component<components::Sound>(new_entity);
        sound.sound_id     = src_sound->sound_id;
        sound.volume       = src_sound->volume;
        sound.pitch        = src_sound->pitch;
        sound.min_distance = src_sound->min_distance;
        sound.max_distance = src_sound->max_distance;
        sound.rolloff      = src_sound->rolloff;
        sound.spatial      = src_sound->spatial;
        sound.autoplay     = src_sound->autoplay;
        sound.loop         = src_sound->loop;
    }

    auto src_particle_effect = get_component<components::ParticleEffect>(base);
    if (src_particle_effect) {
        auto& particle_effect           = add_component<components::ParticleEffect>(new_entity);
        particle_effect.effect_id       = src_particle_effect->effect_id;
        particle_effect.active          = src_particle_effect->active;
        particle_effect.emitter_configs = src_particle_effect->emitter_configs;
        particle_effect.effect          = src_particle_effect->effect;
        particle_effect.dirty           = true;
    }

    return new_entity;
}

bool Scene::node_has_tag(Entity node, const std::string tag) {
    auto t = get_component<components::Tag>(node);

    if (t) {
        for (const auto& etag : t->tags) {
            if (etag.compare(tag) == 0) {
                return true;
            }
        }
    }

    return false;
}

std::vector<Entity> Scene::find_nodes_with_tag(const std::string tag) {
    auto view = entity_registry.view<components::Tag>();

    std::vector<Entity> nodes;
    for (auto [e, t] : view.each()) {
        for (const auto& etag : t.tags) {
            if (etag.compare(tag) == 0) {
                nodes.push_back(e);
                break;
            }
        }
    }

    return nodes;
}

void Scene::remove_all_components(Entity node) {
    using AllComponents = std::tuple<
        components::Transform,
        components::Parent,
        components::Children,
        components::Name,
        components::Mesh,
        components::Physics,
        components::Script,
        components::Tag>;

    std::apply(
        [&](auto... args) {
            (remove_component<decltype(args)>(node), ...);
        },
        AllComponents{}
    );
}
