#include "asset_registry.hpp"

#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>

AssetRegistry::AssetRegistry() {
    extension_to_type = {
        {".png", AssetType::TEXTURE},
        {".jpg", AssetType::TEXTURE},
        {".jpeg", AssetType::TEXTURE},

        {".glb", AssetType::MODEL},

        {".mat", AssetType::MATERIAL},

        {".as", AssetType::SCRIPT},

        {".ttf", AssetType::FONT},

        {".wav", AssetType::SOUND},
        {".mp3", AssetType::SOUND},
        {".flac", AssetType::SOUND},

        {".pfx", AssetType::PARTICLE_EFFECT},

        {".skel", AssetType::SKELETON},

        {".anim", AssetType::ANIMATION},

        {".ies", AssetType::IES_PROFILE},
    };
}

void AssetRegistry::initialize(class World* world) {
    this->world = world;
}

void AssetRegistry::load(const std::filesystem::path& path) {
    spdlog::info("Loading asset metadata from: {}", path.string());
    this->root = path;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".metadata") {
            spdlog::info("loading {}", entry.path().string());

            std::ifstream metadata(entry.path());
            AssetType     type = AssetType::UNSUPPORTED;
            {
                cereal::JSONInputArchive peek_archive(metadata);
                peek_archive.startNode();
                peek_archive(cereal::make_nvp("type", type));
            }

            metadata.clear();
            metadata.seekg(0);
            cereal::JSONInputArchive archive(metadata);

            MetadataHandle handle = nullptr;
            switch (type) {
            case AssetType::UNSUPPORTED:
                break;
            case AssetType::TEXTURE: {
                auto texture_metadata = std::make_unique<TextureMetadata>();
                archive(*texture_metadata);
                handle = std::move(texture_metadata);
                break;
            }
            case AssetType::MESH: {
                auto mesh_metadata = std::make_unique<MeshMetadata>();
                archive(*mesh_metadata);
                handle = std::move(mesh_metadata);
                break;
            }
            case AssetType::MODEL: {
                auto model_metadata = std::make_unique<ModelMetadata>();
                archive(*model_metadata);
                handle = std::move(model_metadata);
                break;
            }
            case AssetType::MATERIAL: {
                auto material_metadata = std::make_unique<MaterialMetadata>();
                archive(*material_metadata);
                handle = std::move(material_metadata);
                break;
            }
            case AssetType::SHADER:
                break;
            case AssetType::SCRIPT:
                break;
            case AssetType::SOUND: {
                auto sound_metadata = std::make_unique<SoundMetadata>();
                archive(*sound_metadata);
                handle = std::move(sound_metadata);
                break;
            }
            case AssetType::FONT: {
                auto font_metadata = std::make_unique<FontMetadata>();
                archive(*font_metadata);
                handle = std::move(font_metadata);
                break;
            }
            case AssetType::PARTICLE_EFFECT: {
                auto particle_effect_metadata = std::make_unique<ParticleEffectMetadata>();
                archive(*particle_effect_metadata);
                handle = std::move(particle_effect_metadata);
                break;
            }
            case AssetType::SKELETON: {
                auto skeleton_metadata = std::make_unique<SkeletonMetadata>();
                archive(*skeleton_metadata);
                handle = std::move(skeleton_metadata);
                break;
            }
            case AssetType::ANIMATION: {
                auto animation_metadata = std::make_unique<AnimationMetadata>();
                archive(*animation_metadata);
                handle = std::move(animation_metadata);
                break;
            }
            case AssetType::IES_PROFILE: {
                auto ies_metadata = std::make_unique<IESProfileMetadata>();
                archive(*ies_metadata);
                handle = std::move(ies_metadata);
                break;
            }
            }

            if (handle) {
                auto id = handle->id;
                register_asset(id, std::move(handle));
            } else {
                spdlog::warn("Unsupported metadata type for {}", entry.path().string());
            }
        }
    }

    scan_for_scripts();
}

AssetID AssetRegistry::hash_path(const std::filesystem::path& path) {
    constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME        = 1099511628211ULL;

    auto normalized_path = relative_path(path).generic_string();

    uint64_t hash = FNV_OFFSET_BASIS;
    for (char c : normalized_path) {
        hash ^= static_cast<uint64_t>(c);
        hash *= FNV_PRIME;
    }

    return hash;
}

AssetType AssetRegistry::extension_to_asset_type(const std::filesystem::path& path) {
    auto it = extension_to_type.find(path.extension().string());

    return it == extension_to_type.end() ? AssetType::UNSUPPORTED : it->second;
}

std::filesystem::path AssetRegistry::source_asset_path() {
    return root / "assets";
}

std::filesystem::path AssetRegistry::stored_asset_path() {
    return root / ".assets";
}

std::filesystem::path AssetRegistry::metadata_path() {
    return root / ".metadata";
}

std::filesystem::path AssetRegistry::root_path() {
    return root;
}

std::filesystem::path AssetRegistry::relative_path(const std::filesystem::path& path) {
    return path.lexically_relative(root).generic_string();
}

void AssetRegistry::register_asset(AssetID id, MetadataHandle metadata) {
    if (!metadata->is_valid()) {
        spdlog::warn("Invalid metadata: {}", metadata->source_path);
        return;
    }
    spdlog::debug("Registering asset {}: {}", metadata->id, metadata->source_path);

    metadata_store[id] = std::move(metadata);
}

void AssetRegistry::scan_for_scripts() {
    if (!std::filesystem::exists(source_asset_path() / "scripts")) {
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_asset_path() / "scripts")) {
        if (!entry.is_regular_file()) {
            continue;
        }

        if (entry.path().extension() != ".as") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) {
            continue;
        }

        AssetID id = hash_path(entry.path());

        auto meta         = std::make_unique<ScriptMetadata>();
        meta->id          = id;
        meta->source_path = entry.path().string();
        meta->asset_path  = entry.path().string();
        meta->type        = AssetType::SCRIPT;
        meta->standalone  = true;

        register_asset(id, std::move(meta));
    }
}
