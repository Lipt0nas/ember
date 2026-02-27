#pragma once

#include "asset.hpp"
#include "ember.hpp"

class AssetImporter {
public:
    void initialize(class World* world);

    AssetID
    import_texture(const std::filesystem::path& path, const TextureMetadata::TextureImportOptions& import_options);

    AssetID import_model(const std::filesystem::path& path, const ModelMetadata::ModelImportOptions& import_options);

    AssetID import_font(const std::filesystem::path& path, const FontMetadata::FontImportOptions& import_options);

private:
    bool process_texture(
        const std::filesystem::path&                 destination,
        int                                          width,
        int                                          height,
        int                                          channels,
        unsigned char*                               data,
        const TextureMetadata::TextureImportOptions& import_options
    );

    World* world = nullptr;
};
