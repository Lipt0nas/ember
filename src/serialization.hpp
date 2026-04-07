#pragma once

#include "ember.hpp"

#include <cereal/cereal.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>

#include "physics.hpp"

namespace JPH {
    template <typename Archive> void serialize(Archive& archive, BodyID& body) {
        uint32_t id = body.GetIndexAndSequenceNumber();

        archive(cereal::make_nvp("id", body.GetIndexAndSequenceNumber()));

        if constexpr (Archive::is_loading::value) {
            body = JPH::BodyID(id);
        }
    }
} // namespace JPH

namespace glm {
    template <class Archive> void serialize(Archive& archive, glm::vec2& v) {
        archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y));
    }

    template <class Archive> void serialize(Archive& archive, glm::vec3& v) {
        archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y), cereal::make_nvp("z", v.z));
    }

    template <class Archive> void serialize(Archive& archive, glm::ivec3& v) {
        archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y), cereal::make_nvp("z", v.z));
    }

    template <class Archive> void serialize(Archive& archive, glm::vec4& v) {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w)
        );
    }

    template <class Archive> void serialize(Archive& archive, glm::quat& v) {
        archive(
            cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w)
        );
    }
} // namespace glm

template <typename Archive> void serialize(Archive& archive, ImVec2& vec) {
    archive(cereal::make_nvp("x", vec.x), cereal::make_nvp("y", vec.y));
}
