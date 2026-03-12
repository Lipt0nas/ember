#pragma once

#include "asset.hpp"
#include "ember.hpp"
#include "resources.hpp"

#include "particle_editor.hpp"

template <typename Archive> void serialize(Archive& archive, ImVec2& vec) {
    if constexpr (cereal::traits::is_text_archive<Archive>::value) {
        archive(cereal::make_nvp("x", vec.x), cereal::make_nvp("y", vec.y));
    } else {
        archive(vec.x, vec.y);
    }
}

class AssetExporter {
public:
    void initialize(class World* world);

    void export_particle_effect(
        const ParticleEffectSaveData& source_data, const ParticleEffectAsset& asset, const std::filesystem::path& name
    );

private:
    World* world = nullptr;
};
