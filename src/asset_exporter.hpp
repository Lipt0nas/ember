#pragma once

#include "asset.hpp"
#include "ember.hpp"
#include "resources.hpp"
#include "serialization.hpp"

#include "particle_editor.hpp"

class AssetExporter {
public:
    void initialize(class World* world);

    void export_particle_effect(
        const ParticleEffectSaveData& source_data, const ParticleEffectAsset& asset, const std::filesystem::path& name
    );

private:
    World* world = nullptr;
};
