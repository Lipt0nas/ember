#include "asset.hpp"

bool AssetMetadata::is_valid() const {
    return id != INVALID_METADATA;
}
