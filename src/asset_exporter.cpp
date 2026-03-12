#include "asset_exporter.hpp"

#include "world.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>

void AssetExporter::initialize(class World* world) {
    this->world = world;
}

void AssetExporter::export_particle_effect(
    const ParticleEffectSaveData& source_data, const ParticleEffectAsset& asset, const std::filesystem::path& name
) {
    spdlog::info("Exporting particle effect: {}", name.string());

    auto source_destination =
        world->asset_registry.source_asset_path() / "particle_effects" / (name.filename().string() + ".json");
    auto hash = world->asset_registry.hash_path(source_destination);

    auto processed_destination =
        world->asset_registry.stored_asset_path() / "particle_effects" / (std::to_string(hash) + ".pfx");
    auto metadata_destination =
        world->asset_registry.metadata_path() / "particle_effects" / (std::to_string(hash) + ".metadata");

    std::filesystem::create_directories(source_destination.parent_path());
    std::filesystem::create_directories(processed_destination.parent_path());
    std::filesystem::create_directories(metadata_destination.parent_path());

    {
        std::ofstream             asset_file(source_destination);
        cereal::JSONOutputArchive archive(asset_file);

        archive(source_data);
    }

    {
        std::ofstream               asset_file(processed_destination, std::ios::binary);
        cereal::BinaryOutputArchive archive(asset_file);

        ParticleEffectHeader effect_header = {
            .emmiter_count = static_cast<uint32_t>(asset.emitters.size()),
        };

        archive(effect_header);
        for (const auto& emitter : asset.emitters) {
            ParticleEmitterHeader header = {
                .spawn_register_count    = static_cast<uint32_t>(emitter.spawn_register_state.size()),
                .update_register_count   = static_cast<uint32_t>(emitter.update_register_state.size()),
                .spawn_instruction_size  = emitter.spawn_instructions.size() * sizeof(ParticleInstruction),
                .update_instruction_size = emitter.update_instructions.size() * sizeof(ParticleInstruction),
            };

            archive(header);
            archive.saveBinary(
                emitter.spawn_register_state.data(), emitter.spawn_register_state.size() * sizeof(glm::vec4)
            );
            archive.saveBinary(
                emitter.spawn_instructions.data(), emitter.spawn_instructions.size() * sizeof(ParticleInstruction)
            );
            archive.saveBinary(
                emitter.update_register_state.data(), emitter.update_register_state.size() * sizeof(glm::vec4)
            );
            archive.saveBinary(
                emitter.update_instructions.data(), emitter.update_instructions.size() * sizeof(ParticleInstruction)
            );
        }
    }

    {
        std::ofstream             metadata_file(metadata_destination);
        cereal::JSONOutputArchive archive(metadata_file);

        std::unique_ptr<ParticleEffectMetadata> metadata = std::make_unique<ParticleEffectMetadata>();
        metadata->id                                     = hash;
        metadata->source_path                            = source_destination.string();
        metadata->asset_path                             = processed_destination.string();
        metadata->type                                   = AssetType::PARTICLE_EFFECT;
        metadata->source_timestamp                       = std::chrono::system_clock::now().time_since_epoch().count();
        metadata->imported_timestamp                     = std::chrono::system_clock::now().time_since_epoch().count();
        metadata->standalone                             = true;
        archive(cereal::make_nvp("metadata", *metadata));

        world->asset_registry.register_asset(hash, std::move(metadata));
        world->reload_particle_effect(hash);
    }
}
