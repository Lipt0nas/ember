#pragma once

#include "asset.hpp"
#include "ember.hpp"

#include <filesystem>

using MetadataHandle = std::unique_ptr<AssetMetadata>;

class AssetRegistry {
public:
    AssetRegistry();

    void initialize(class World* world);

    void load(const std::filesystem::path& path);
    void scan_for_scripts();

    AssetID hash_path(const std::filesystem::path& path);

    AssetType extension_to_asset_type(const std::filesystem::path& path);

    std::filesystem::path source_asset_path();
    std::filesystem::path stored_asset_path();
    std::filesystem::path metadata_path();
    std::filesystem::path root_path();

    void register_asset(AssetID id, MetadataHandle metadata);

    template <typename T> const T* get_metadata(AssetID id) {
        auto it = metadata_store.find(id);

        return it == metadata_store.end() ? nullptr : dynamic_cast<T*>(it->second.get());
    }

    const auto& get_metadata_store() {
        return metadata_store;
    }

private:
    std::unordered_map<std::string, AssetType>  extension_to_type;
    std::unordered_map<AssetID, MetadataHandle> metadata_store;

    std::filesystem::path root;

    World* world = nullptr;
};
