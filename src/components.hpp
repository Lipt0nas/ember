#pragma once

#include "asset.hpp"
#include "camera.hpp"
#include "ember.hpp"
#include "geometry.hpp"
#include "physics.hpp"

#include <memory>
#include <variant>
#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

namespace JPH {
    template <typename Archive> void serialize(Archive& archive, BodyID& body) {
        uint32_t id = body.GetIndexAndSequenceNumber();

        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("id", body.GetIndexAndSequenceNumber()));
        } else {
            archive(id);
        }

        if constexpr (Archive::is_loading::value) {
            body = JPH::BodyID(id);
        }
    }
} // namespace JPH

struct ScriptProperty {
    std::variant<bool, int, float, std::string, glm::vec2, glm::vec3, glm::vec4, glm::quat> value;
};

template <typename Archive> void serialize(Archive& archive, ScriptProperty& prop) {
    archive(prop.value);
}

struct ScriptInstance {
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

    template <class Archive> void serialize(Archive& archive) {
        archive(
            CEREAL_NVP(albedo_factor),
            CEREAL_NVP(emissive_factor),
            CEREAL_NVP(roughness_factor),
            CEREAL_NVP(metallic_factor),
            CEREAL_NVP(normal_scale)
        );
    }
};

template <typename Archive> void serialize(Archive& archive, ScriptInstance& script) {
    if constexpr (cereal::traits::is_text_archive<Archive>::value) {
        archive(
            cereal::make_nvp("id", script.script_id), cereal::make_nvp("property_overrides", script.property_overrides)
        );
    } else {
        archive(script.script_id, script.property_overrides);
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
        bool        is_static;

        // NOTE: used for interpolation, though maybe this could be moved to transform?
        JPH::Vec3 last_position;
        JPH::Quat last_rotation;

        float last_scale = 1.0f;
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
    };

    struct CharacterController {
        float height = 1.8f;
        float radius = 0.5f;

        float step_down_distance    = 0.5f;
        float step_up_height        = 0.4f;
        float max_slope_angle       = 45.0f;
        bool  enhanced_edge_removal = true;

        uint32_t collision_layer = 0;

        JPH::CharacterVirtual* controller    = nullptr;
        glm::vec3              velocity      = glm::vec3(0.0f);
        glm::vec3              ground_normal = glm::vec3(0.0f, 1.0f, 0.0f);
        bool                   is_grounded   = false;
    };

    template <typename Archive> void serialize(Archive& archive, Transform& transform) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(
                cereal::make_nvp("position", transform.position),
                cereal::make_nvp("scale", transform.scale),
                cereal::make_nvp("rotation", transform.rotation)
            );
        } else {
            archive(transform.position, transform.scale, transform.rotation);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Name& name) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("name", name.name));
        } else {
            archive(name.name);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Material& mat) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("id", mat.id), cereal::make_nvp("overrides", mat.overrides));
        } else {
            archive(mat.id, mat.overrides);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Mesh& mesh) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("id", mesh.id));
        } else {
            archive(mesh.id);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Parent& parent) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("parent", parent.parent));
        } else {
            archive(parent.parent);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Children& children) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("children", children.children));
        } else {
            archive(children.children);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Physics& physics) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(
                cereal::make_nvp("is_static", physics.is_static), cereal::make_nvp("last_scale", physics.last_scale)
            );
        } else {
            archive(physics.body_id, physics.is_static, physics.last_scale);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Script& script) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("scripts", script.scripts));
        } else {
            archive(script.scripts);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Tag& tag) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("tags", tag.tags));
        } else {
            archive(tag.tags);
        }
    }

    template <typename Archive> void serialize(Archive& archive, Camera& camera) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
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
                cereal::make_nvp("is_active", camera.is_active)
            );
        } else {
            archive(
                camera.near_plane,
                camera.far_plane,
                camera.fov,
                camera.viewport_x,
                camera.viewport_y,
                camera.viewport_width,
                camera.viewport_height,
                camera.ortho_size,
                camera.type,
                camera.is_active
            );
        }
    }

    template <typename Archive> void serialize(Archive& archive, CharacterController& controller) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(
                cereal::make_nvp("height", controller.height),
                cereal::make_nvp("radius", controller.radius),
                cereal::make_nvp("step_down_distance", controller.step_down_distance),
                cereal::make_nvp("step_up_height", controller.step_up_height),
                cereal::make_nvp("max_slope_angle", controller.max_slope_angle),
                cereal::make_nvp("collision_layer", controller.collision_layer)
            );
        } else {
            archive(
                controller.height,
                controller.radius,
                controller.step_down_distance,
                controller.step_up_height,
                controller.max_slope_angle,
                controller.collision_layer
            );
        }
    }

} // namespace components
