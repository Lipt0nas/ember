#pragma once

#include "ember.hpp"
#include "geometry.hpp"
#include "physics.hpp"

#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>

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

    struct Mesh {
        MeshInstance mesh;
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
        uint32_t script_id;

        // NOTE: maybe script system could just keep the instances
        void* object = nullptr;
    };

    struct Tag {
        std::vector<std::string> tags;
    };

    template <typename Archive> void serialize(Archive& archive, Transform& transform) {
        archive(
            transform.position.x,
            transform.position.y,
            transform.position.z,
            transform.scale,
            transform.rotation.x,
            transform.rotation.y,
            transform.rotation.z,
            transform.rotation.w
        );
    }

    template <typename Archive> void serialize(Archive& archive, Name& name) {
        archive(name.name);
    }

    template <typename Archive> void serialize(Archive& archive, Mesh& mesh) {
        archive(mesh.mesh.mesh_id, mesh.mesh.material_id);
    }

    template <typename Archive> void serialize(Archive& archive, Parent& parent) {
        archive(parent.parent);
    }

    template <typename Archive> void serialize(Archive& archive, Children& children) {
        archive(children.children);
    }

    template <typename Archive> void serialize(Archive& archive, Physics& physics) {
        archive(physics.is_static, physics.last_scale);
    }

    template <typename Archive> void serialize(Archive& archive, Script& script) {
        archive(script.script_id);
    }

    template <typename Archive> void serialize(Archive& archive, Tag& tag) {
        archive(tag.tags);
    }

} // namespace components
