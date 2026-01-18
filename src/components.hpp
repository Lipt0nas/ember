#pragma once

#include "ember.hpp"
#include "geometry.hpp"
#include "physics.hpp"

namespace components {
    struct Transform {
        glm::vec3 position;
        float     scale;

        glm::quat rotation;
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
    };
} // namespace components
