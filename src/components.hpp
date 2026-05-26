#pragma once

#include "asset.hpp"
#include "camera.hpp"
#include "ember.hpp"
#include "geometry.hpp"
#include "particle_editor.hpp"
#include "physics.hpp"
#include "serialization.hpp"
#include "sound_system.hpp"

#include <memory>
#include <variant>
#include <vector>

enum class PhysicsColliderType : uint32_t {
    NONE = 0,
    MESH,
    VHACD,
    BOX,
    CAPSULE,
    SPHERE,
};

enum class PhysicsMotionType : uint32_t {
    STATIC = 0,
    KINEMATIC,
    DYNAMIC,
};

struct ScriptProperty {
    std::variant<bool, int, float, std::string, glm::vec2, glm::vec3, glm::vec4, glm::quat> value;
};

template <typename Archive> void serialize(Archive& archive, ScriptProperty& prop, const uint32_t version) {
    archive(cereal::make_nvp("value0", prop.value));
}

struct ScriptInstance {
    bool active = true;

    AssetID                                         script_id;
    std::unordered_map<std::string, ScriptProperty> property_overrides;
    void*                                           object = nullptr;
};

struct MaterialOverrides {
    std::optional<glm::vec4> albedo_factor;
    std::optional<glm::vec3> emissive_factor;
    std::optional<float>     roughness_factor;
    std::optional<float>     metallic_factor;
    std::optional<float>     normal_scale;

    bool active() {
        return albedo_factor.has_value() || emissive_factor.has_value() || roughness_factor.has_value() ||
               metallic_factor.has_value() || normal_scale.has_value();
    }

    template <class Archive> void serialize(Archive& archive, const uint32_t version) {
        archive(
            CEREAL_NVP(albedo_factor),
            CEREAL_NVP(emissive_factor),
            CEREAL_NVP(roughness_factor),
            CEREAL_NVP(metallic_factor),
            CEREAL_NVP(normal_scale)
        );
    }
};

template <typename Archive> void serialize(Archive& archive, ScriptInstance& script, const uint32_t version) {
    archive(
        cereal::make_nvp("id", script.script_id), cereal::make_nvp("property_overrides", script.property_overrides)
    );

    if (version > 0) {
        archive(cereal::make_nvp("active", script.active));
    }
}

namespace components {
    struct Transform {
        glm::vec3 position = {};
        float     scale    = 1.0f;
        glm::quat rotation = {0, 0, 0, 1};

        glm::vec3 world_position = {};
        float     world_scale    = 1.0f;
        glm::quat world_rotation = {0, 0, 0, 1};

        bool dirty = false;
    };

    struct Parent {
        entt::entity parent;
    };

    struct Children {
        std::vector<entt::entity> children;
    };

    struct Name {
        std::string name;
    };

    struct Material {
        AssetID id = AssetMetadata::INVALID_METADATA;

        MaterialOverrides overrides;
        int               dedicated_material_index = -1;
    };

    struct Mesh {
        AssetID id = AssetMetadata::INVALID_METADATA;

        MeshInstance instance;
    };

    struct Physics {
        JPH::BodyID body_id;
        glm::vec3   pivot_offset = {};

        // NOTE: technically shouldn't serialize this
        float last_scale = 1.0f;

        PhysicsColliderType collider_type = PhysicsColliderType::BOX;

        glm::vec3 box_half_extent     = {1.0f, 1.0f, 1.0f};
        float     capsule_radius      = 1.0f;
        float     capsule_half_height = 1.0f;
        float     sphere_radius       = 1.0f;

        float friction        = 0.5f;
        float restitution     = 0.0f;
        float linear_damping  = 0.05f;
        float angular_damping = 0.05f;
        float mass_override   = 0.0f;
        float density         = 1000.0f;

        PhysicsMotionType motion_type = PhysicsMotionType::STATIC;
        uint16_t          layer       = Layers::NON_MOVING;

        // NOTE: used for interpolation, though maybe this could be moved to transform?
        JPH::Vec3 last_position = {};
        JPH::Quat last_rotation = {};
    };

    struct Script {
        std::vector<ScriptInstance> scripts;
    };

    struct Tag {
        std::vector<std::string> tags;
    };

    struct Camera {
        float near_plane = 0.1f;
        float far_plane  = 1000.0f;
        float fov        = 60.0f;

        float viewport_x      = 0.0f;
        float viewport_y      = 0.0f;
        float viewport_width  = 1.0f;
        float viewport_height = 1.0f;
        float ortho_size      = 10.0f;

        CameraType type = CameraType::PERSPECTIVE;

        bool is_active = false;

        float ev_compensation = 0.0f;
        bool  manual_exposure = false;

        // Manual exposure settings
        float aperture     = 8.0f;
        float shutter_time = 1.0f / 60.0f;
        float iso          = 100.0f;

        // Automatic exposure settings
        float min_log_luminance = 2.0f;
        float max_log_luminance = 14.0f;
        float adaption_speed    = 1.24f;

        float min_ev100 = -2.0f;
        float max_ev100 = 16.0f;
    };

    struct CharacterController {
        float height = 1.8f;
        float radius = 0.5f;

        float step_down_distance    = 0.5f;
        float step_up_height        = 0.4f;
        float max_slope_angle       = 45.0f;
        float gravity_scale         = 1.0f;
        bool  enhanced_edge_removal = true;

        float mass     = 70.0f;
        float strength = 170.0f;
        float padding  = 0.05f;

        uint32_t collision_layer = 0;

        JPH::CharacterVirtual* controller    = nullptr;
        glm::vec3              velocity      = glm::vec3(0.0f);
        glm::vec3              ground_normal = glm::vec3(0.0f, 1.0f, 0.0f);
        bool                   is_grounded   = false;
    };

    struct Sprite {
        glm::vec2 size  = {10.0f, 10.0f};
        glm::vec2 pivot = {0.5f, 0.5f};
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 uvs   = {0.0f, 0.0f, 1.0f, 1.0f};

        AssetID texture_id = AssetMetadata::INVALID_METADATA;
    };

    struct Text {
        std::string text  = "Text";
        glm::vec2   pivot = {0.5f, 0.5f};
        glm::vec4   color = {1.0f, 1.0f, 1.0f, 1.0f};

        AssetID font_id = AssetMetadata::INVALID_METADATA;
    };

    struct World {
        bool _dummy;
    };

    struct Sound {
        AssetID sound_id = AssetMetadata::INVALID_METADATA;

        float volume       = 1.0f;
        float pitch        = 1.0f;
        float min_distance = 1.0f;
        float max_distance = 50.0f;
        float rolloff      = 1.0f;
        bool  spatial      = true;
        bool  autoplay     = false;
        bool  loop         = false;

        int instance_id = SoundSystem::INVALID_SOUND_INSTANCE;
    };

    struct ParticleEffect {
        AssetID effect_id = AssetMetadata::INVALID_METADATA;

        bool                               active = true;
        std::vector<ParticleEmitterConfig> emitter_configs;

        std::optional<::ParticleEffect> effect;
        bool                            dirty = false; // Should the effect be reloaded from the base effect
    };

    struct Light {
        AssetID ies_profile = AssetMetadata::INVALID_METADATA;
        ::Light light;
    };

    struct SkeletalAnimation {
        AssetID skeleton_id  = AssetMetadata::INVALID_METADATA;
        AssetID animation_id = AssetMetadata::INVALID_METADATA;

        bool looping = true;

        float speed = 1.0f;

        float time = 0.0f;
    };

    struct DirectionalLight {
        bool enabled = true;

        glm::vec4 color = {1, 1, 1, 10};
    };

    struct Sky {
        glm::vec4 top_hemisphere_color    = {1, 1, 1, 1};
        glm::vec4 bottom_hemisphere_color = {0, 0, 0, 1};
    };

    struct DDGIVolume {
        ::DDGIVolume volume;
    };

    template <typename Archive> void serialize(Archive& archive, Transform& transform, const uint32_t version) {
        archive(
            cereal::make_nvp("position", transform.position),
            cereal::make_nvp("scale", transform.scale),
            cereal::make_nvp("rotation", transform.rotation)
        );
    }

    template <typename Archive> void serialize(Archive& archive, Name& name, const uint32_t version) {
        archive(cereal::make_nvp("name", name.name));
    }

    template <typename Archive> void serialize(Archive& archive, Material& mat, const uint32_t version) {
        archive(cereal::make_nvp("id", mat.id), cereal::make_nvp("overrides", mat.overrides));
    }

    template <typename Archive> void serialize(Archive& archive, Mesh& mesh, const uint32_t version) {
        archive(cereal::make_nvp("id", mesh.id));
    }

    template <typename Archive> void serialize(Archive& archive, Parent& parent, const uint32_t version) {
        archive(cereal::make_nvp("parent", parent.parent));
    }

    template <typename Archive> void serialize(Archive& archive, Children& children, const uint32_t version) {
        archive(cereal::make_nvp("children", children.children));
    }

    template <typename Archive> void serialize(Archive& archive, Physics& physics, const uint32_t version) {
        if constexpr (!cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("body_id", physics.body_id));
            archive(cereal::make_nvp("pivot_offset", physics.pivot_offset));
        }

        archive(
            cereal::make_nvp("last_scale", physics.last_scale),
            cereal::make_nvp("collider_type", physics.collider_type),
            cereal::make_nvp("box_half_extent", physics.box_half_extent),
            cereal::make_nvp("capsule_radius", physics.capsule_radius),
            cereal::make_nvp("capsule_half_height", physics.capsule_half_height),
            cereal::make_nvp("sphere_radius", physics.sphere_radius),
            cereal::make_nvp("friction", physics.friction),
            cereal::make_nvp("restitution", physics.restitution),
            cereal::make_nvp("linear_damping", physics.linear_damping),
            cereal::make_nvp("angular_damping", physics.angular_damping),
            cereal::make_nvp("mass_override", physics.mass_override),
            cereal::make_nvp("density", physics.density),
            cereal::make_nvp("motion_type", physics.motion_type),
            cereal::make_nvp("layer", physics.layer)
        );
    }

    template <typename Archive> void serialize(Archive& archive, Script& script, const uint32_t version) {
        archive(cereal::make_nvp("scripts", script.scripts));
    }

    template <typename Archive> void serialize(Archive& archive, Tag& tag, const uint32_t version) {
        archive(cereal::make_nvp("tags", tag.tags));
    }

    template <typename Archive> void serialize(Archive& archive, Camera& camera, const uint32_t version) {
        archive(
            cereal::make_nvp("near_plane", camera.near_plane),
            cereal::make_nvp("far_plane", camera.far_plane),
            cereal::make_nvp("fov", camera.fov),
            cereal::make_nvp("viewport_x", camera.viewport_x),
            cereal::make_nvp("viewport_y", camera.viewport_y),
            cereal::make_nvp("viewport_width", camera.viewport_width),
            cereal::make_nvp("viewport_height", camera.viewport_height),
            cereal::make_nvp("ortho_size", camera.ortho_size),
            cereal::make_nvp("type", camera.type),
            cereal::make_nvp("is_active", camera.is_active),
            cereal::make_nvp("ev_compensation", camera.ev_compensation),
            cereal::make_nvp("manual_exposure", camera.manual_exposure),
            cereal::make_nvp("aperture", camera.aperture),
            cereal::make_nvp("shutter_time", camera.shutter_time),
            cereal::make_nvp("iso", camera.iso),
            cereal::make_nvp("min_log_luminance", camera.min_log_luminance),
            cereal::make_nvp("max_log_luminance", camera.max_log_luminance),
            cereal::make_nvp("adaption_speed", camera.adaption_speed),
            cereal::make_nvp("min_ev100", camera.min_ev100),
            cereal::make_nvp("max_ev100", camera.max_ev100)
        );
    }

    template <typename Archive>
    void serialize(Archive& archive, CharacterController& controller, const uint32_t version) {
        archive(
            cereal::make_nvp("height", controller.height),
            cereal::make_nvp("radius", controller.radius),
            cereal::make_nvp("step_down_distance", controller.step_down_distance),
            cereal::make_nvp("step_up_height", controller.step_up_height),
            cereal::make_nvp("max_slope_angle", controller.max_slope_angle),
            cereal::make_nvp("gravity_scale", controller.gravity_scale),
            cereal::make_nvp("collision_layer", controller.collision_layer)
        );

        if (version > 0) {
            archive(
                cereal::make_nvp("mass", controller.mass),
                cereal::make_nvp("strength", controller.strength),
                cereal::make_nvp("padding", controller.padding)
            );
        }
    }

    template <typename Archive> void serialize(Archive& archive, Sprite& sprite, const uint32_t version) {
        archive(
            cereal::make_nvp("size", sprite.size),
            cereal::make_nvp("anchor", sprite.pivot),
            cereal::make_nvp("color", sprite.color),
            cereal::make_nvp("uvs", sprite.uvs),
            cereal::make_nvp("texture_id", sprite.texture_id)
        );
    }

    template <typename Archive> void serialize(Archive& archive, Text& text, const uint32_t version) {
        archive(
            cereal::make_nvp("text", text.text),
            cereal::make_nvp("color", text.color),
            cereal::make_nvp("pivot", text.pivot),
            cereal::make_nvp("font_id", text.font_id)
        );
    }

    template <typename Archive> void serialize(Archive& archive, Sound& sound, const uint32_t version) {
        archive(
            cereal::make_nvp("id", sound.sound_id),
            cereal::make_nvp("volume", sound.volume),
            cereal::make_nvp("pitch", sound.pitch),
            cereal::make_nvp("min_distance", sound.min_distance),
            cereal::make_nvp("max_distance", sound.max_distance),
            cereal::make_nvp("rolloff", sound.rolloff),
            cereal::make_nvp("spatial", sound.spatial),
            cereal::make_nvp("autoplay", sound.autoplay),
            cereal::make_nvp("loop", sound.loop)
        );
    }

    template <typename Archive> void serialize(Archive& archive, ParticleEffect& particle, const uint32_t version) {
        archive(
            cereal::make_nvp("effect_id", particle.effect_id),
            cereal::make_nvp("active", particle.active),
            cereal::make_nvp("configs", particle.emitter_configs)
        );
    }

    template <typename Archive> void serialize(Archive& archive, World& world, const uint32_t version) {
    }

    template <typename Archive> void serialize(Archive& archive, Light& light, const uint32_t version) {
        archive(cereal::make_nvp("ies_profile", light.ies_profile), cereal::make_nvp("light", light.light));
    }

    template <typename Archive> void serialize(Archive& archive, SkeletalAnimation& animation, const uint32_t version) {
        archive(
            cereal::make_nvp("skeleton_id", animation.skeleton_id),
            cereal::make_nvp("animation_id", animation.animation_id),
            cereal::make_nvp("looping", animation.looping),
            cereal::make_nvp("speed", animation.speed)
        );
    }

    template <typename Archive>
    void serialize(Archive& archive, DirectionalLight& directional, const uint32_t version) {
        archive(cereal::make_nvp("enabled", directional.enabled), cereal::make_nvp("color", directional.color));
    }

    template <typename Archive> void serialize(Archive& archive, Sky& sky, const uint32_t version) {
        archive(
            cereal::make_nvp("top_hemisphere_color", sky.top_hemisphere_color),
            cereal::make_nvp("bottom_hemisphere_color", sky.bottom_hemisphere_color)
        );
    }

    template <typename Archive> void serialize(Archive& archive, DDGIVolume& volume, const uint32_t version) {
        archive(cereal::make_nvp("volume", volume.volume));
    }
} // namespace components

CEREAL_CLASS_VERSION(ScriptProperty, 0)
CEREAL_CLASS_VERSION(MaterialOverrides, 0)
CEREAL_CLASS_VERSION(ScriptInstance, 1)
CEREAL_CLASS_VERSION(components::Transform, 0)
CEREAL_CLASS_VERSION(components::Name, 0)
CEREAL_CLASS_VERSION(components::Material, 0)
CEREAL_CLASS_VERSION(components::Mesh, 0)
CEREAL_CLASS_VERSION(components::Parent, 0)
CEREAL_CLASS_VERSION(components::Children, 0)
CEREAL_CLASS_VERSION(components::Physics, 0)
CEREAL_CLASS_VERSION(components::Script, 0)
CEREAL_CLASS_VERSION(components::Tag, 0)
CEREAL_CLASS_VERSION(components::Camera, 0)
CEREAL_CLASS_VERSION(components::CharacterController, 1)
CEREAL_CLASS_VERSION(components::Sprite, 0)
CEREAL_CLASS_VERSION(components::Text, 0)
CEREAL_CLASS_VERSION(components::Sound, 0)
CEREAL_CLASS_VERSION(components::ParticleEffect, 0)
CEREAL_CLASS_VERSION(components::World, 0)
CEREAL_CLASS_VERSION(components::Light, 0)
CEREAL_CLASS_VERSION(components::SkeletalAnimation, 0)
CEREAL_CLASS_VERSION(components::DirectionalLight, 0)
CEREAL_CLASS_VERSION(components::Sky, 0)
CEREAL_CLASS_VERSION(components::DDGIVolume, 0)
