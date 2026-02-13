#pragma once

#include "ember.hpp"
#include "physics.hpp"
#include "scene.hpp"
#include "script_system.hpp"

class SceneSerializer {
public:
    static void load(const std::filesystem::path& path, class World& world);

    static void save(const std::filesystem::path& path, class World& world);
};
