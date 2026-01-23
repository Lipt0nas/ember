#pragma once

#include "ember.hpp"
#include "geometry.hpp"
#include "physics.hpp"

#include <vector>

namespace components {
    struct Transform {
        glm::vec3 position = {};
        float     scale    = 1.0f;
        glm::quat rotation = {0, 0, 0, 1};

        glm::vec3 world_position;
        float     world_scale;
        glm::quat world_rotation;
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
} // namespace components
