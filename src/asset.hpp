#pragma once

#include "ember.hpp"

#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

using AssetID = uint64_t;

struct SamplerDescription {
    VkFilter             filter       = VK_FILTER_LINEAR;
    VkSamplerAddressMode address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkSamplerMipmapMode  mipmap_mode  = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    float                anisotropy   = 16.0f;

    template <class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(filter), CEREAL_NVP(address_mode), CEREAL_NVP(mipmap_mode), CEREAL_NVP(anisotropy));
    }

    size_t state_hash() const {
        size_t h1 = std::hash<int>()(filter);
        size_t h2 = std::hash<int>()(address_mode);
        size_t h3 = std::hash<int>()(mipmap_mode);
        size_t h4 = std::hash<float>()(anisotropy);

        size_t result = h1;
        result ^= h2 + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
        result ^= h4 + 0x9e3779b9 + (result << 6) + (result >> 2);

        return result;
    }
};

namespace glm {
    template <class Archive> void serialize(Archive& archive, glm::vec2& v) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y));
        } else {
            archive(v.x, v.y);
        }
    }

    template <class Archive> void serialize(Archive& archive, glm::vec3& v) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(cereal::make_nvp("x", v.x), cereal::make_nvp("y", v.y), cereal::make_nvp("z", v.z));
        } else {
            archive(v.x, v.y, v.z);
        }
    }

    template <class Archive> void serialize(Archive& archive, glm::vec4& v) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(
                cereal::make_nvp("x", v.x),
                cereal::make_nvp("y", v.y),
                cereal::make_nvp("z", v.z),
                cereal::make_nvp("w", v.w)
            );
        } else {
            archive(v.x, v.y, v.z, v.w);
        }
    }

    template <class Archive> void serialize(Archive& archive, glm::quat& v) {
        if constexpr (cereal::traits::is_text_archive<Archive>::value) {
            archive(
                cereal::make_nvp("x", v.x),
                cereal::make_nvp("y", v.y),
                cereal::make_nvp("z", v.z),
                cereal::make_nvp("w", v.w)
            );
        } else {
            archive(v.x, v.y, v.z, v.w);
        }
    }
} // namespace glm

enum class AssetType {
    UNSUPPORTED,
    TEXTURE,
    MESH,
    MODEL,
    MATERIAL,
    SHADER,
    SCRIPT,
    SOUND,
    FONT,
    PARTICLE_EFFECT,
    SKELETON,
    ANIMATION,
    IES_PROFILE,
};

class AssetMetadata {
public:
    constexpr inline static uint64_t INVALID_METADATA = UINT64_MAX;

    AssetID     id = INVALID_METADATA;
    std::string source_path;
    std::string asset_path;
    AssetType   type;
    uint64_t    source_timestamp;
    uint64_t    imported_timestamp;
    bool        standalone;

    virtual ~AssetMetadata() = default;

    bool is_valid() const;

    template <class Archive> void serialize(Archive& ar) {
        ar(CEREAL_NVP(id),
           CEREAL_NVP(source_path),
           CEREAL_NVP(asset_path),
           CEREAL_NVP(type),
           CEREAL_NVP(source_timestamp),
           CEREAL_NVP(imported_timestamp),
           CEREAL_NVP(standalone));
    }
};

struct TextureMetadata : AssetMetadata {
    struct TextureImportOptions {
        enum class Compression : uint32_t {
            None = 0,
            BC5,
            BC7
        };

        bool        is_srgb          = true;
        bool        is_normal_map    = false;
        bool        generate_mipmaps = true;
        Compression compression      = Compression::BC7;

        SamplerDescription sampler_description;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(is_srgb),
               CEREAL_NVP(is_normal_map),
               CEREAL_NVP(generate_mipmaps),
               CEREAL_NVP(compression),
               CEREAL_NVP(sampler_description));
        }
    } import_options;

    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
        ar(CEREAL_NVP(import_options));
    }
};

struct MeshMetadata : AssetMetadata {
    struct MeshImportOptions {
        bool generate_lods     = true;
        bool generate_meshlets = true;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(generate_lods), CEREAL_NVP(generate_meshlets));
        }
    } import_options;

    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
        ar(CEREAL_NVP(import_options));
    }
};

struct ModelMetadata : AssetMetadata {
    struct ModelImportOptions {
        TextureMetadata::TextureImportOptions texture_import_options;
        MeshMetadata::MeshImportOptions       mesh_import_options;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(texture_import_options), CEREAL_NVP(mesh_import_options));
        }
    } import_options;

    std::vector<AssetID> mesh_ids;
    std::vector<AssetID> texture_ids;
    std::vector<AssetID> material_ids;

    struct NodeDescription {
        std::string name;
        glm::vec3   position;
        float       scale;
        glm::quat   rotation;

        AssetID mesh_id;
        AssetID material_id;

        std::vector<NodeDescription> children;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(name),
               CEREAL_NVP(position),
               CEREAL_NVP(scale),
               CEREAL_NVP(rotation),
               CEREAL_NVP(mesh_id),
               CEREAL_NVP(material_id),
               CEREAL_NVP(children));
        }
    };

    struct SceneDescription {
        std::vector<NodeDescription> nodes;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(nodes));
        }
    } scene_description;

    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
        ar(CEREAL_NVP(import_options), CEREAL_NVP(mesh_ids), CEREAL_NVP(texture_ids), CEREAL_NVP(scene_description));
    }
};

struct MaterialMetadata : AssetMetadata {};

struct SoundMetadata : AssetMetadata {
    struct SoundImportOptions {
        bool stream = false;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(stream));
        }
    } import_options;

    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
        ar(CEREAL_NVP(import_options));
    }
};

struct ScriptMetadata : AssetMetadata {};

struct FontMetadata : AssetMetadata {
    struct GlyphRange {
        uint32_t first_codepoint;
        uint32_t last_codepoint;

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(first_codepoint), CEREAL_NVP(last_codepoint));
        }
    };

    struct FontImportOptions {
        bool                    is_sdf           = true;
        float                   font_size        = 64.0f;
        std::vector<GlyphRange> character_ranges = {
            {32, 126},
        };

        template <class Archive> void serialize(Archive& ar) {
            ar(CEREAL_NVP(is_sdf), CEREAL_NVP(font_size), CEREAL_NVP(character_ranges));
        }
    } import_options;

    AssetID atlas_texture_id;

    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
        ar(CEREAL_NVP(import_options), CEREAL_NVP(atlas_texture_id));
    }
};

struct ParticleEffectMetadata : AssetMetadata {
    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
    }
};

struct SkeletonMetadata : AssetMetadata {
    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
    }
};

struct AnimationMetadata : AssetMetadata {
    AssetID skeleton_id;

    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
        ar(skeleton_id);
    }
};

struct IESProfileMetadata : AssetMetadata {
    template <class Archive> void serialize(Archive& ar) {
        AssetMetadata::serialize(ar);
    }
};
