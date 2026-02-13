#include "scene_serializer.hpp"

#include "component_registry.hpp"
#include "world.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include <ktx.h>

void SceneSerializer::load(const std::filesystem::path& path, World& world) {
    if (!std::filesystem::is_directory(path)) {
        spdlog::error("{} is not a directory", path.string());
        return;
    }

    spdlog::info("Loading scene {}", path.string());
    spdlog::info("Loading scene data");
    std::ifstream is(path / "scene.json", std::ios::binary);
    if (!is.is_open()) {
        spdlog::error("Could not open scene file for reading");
        return;
    }
    cereal::JSONInputArchive archive(is);

    uint32_t version;
    archive(cereal::make_nvp("version", version));
    spdlog::info("Scene description version: {}", version);

    spdlog::info("Loading nodes");
    size_t node_count;
    archive(cereal::make_nvp("node_count", node_count));

    archive.setNextName("nodes");
    archive.startNode();
    for (size_t i = 0; i < node_count; i++) {
        archive.startNode();

        Entity id;
        archive(cereal::make_nvp("id", id));

        auto node = world.scene.entity_registry.create(id);

        archive.setNextName("components");
        archive.startNode();
        ComponentRegistry::load_node(world, node, archive);
        archive.finishNode();

        archive.finishNode();
    }
    archive.finishNode();
}

void SceneSerializer::save(const std::filesystem::path& path, World& world) {
    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path);
    }

    if (!std::filesystem::is_directory(path)) {
        spdlog::error("{} is not a directory", path.string());
        return;
    }

    spdlog::info("Saving scene to {}", path.string());
    VK_CHECK(vkDeviceWaitIdle(world.gpu.device));

    spdlog::info("Saving scene data");
    std::ofstream os(path / "scene.json", std::ios::binary);
    if (!os.is_open()) {
        spdlog::error("Could not open scene file for writing");
        return;
    }
    cereal::JSONOutputArchive archive(os);

    archive(cereal::make_nvp("version", (uint32_t)0));

    spdlog::info("Saving nodes");
    std::vector<Entity> node_ids;
    auto                view = world.scene.entity_registry.view<entt::entity>();
    for (auto e : view) {
        node_ids.push_back(e);
    }
    archive(cereal::make_nvp("node_count", node_ids.size()));

    archive.setNextName("nodes");
    archive.startNode();
    archive.makeArray();
    for (auto e : node_ids) {
        archive.startNode();
        archive(cereal::make_nvp("id", e));

        archive.setNextName("components");
        archive.startNode();
        ComponentRegistry::save_node(world, e, archive);
        archive.finishNode();

        archive.finishNode();
    }
    archive.finishNode();
}
