#pragma once

#include "ember.hpp"
#include "input_system.hpp"
#include "physics.hpp"
#include "scene.hpp"
#include "script_system.hpp"

class World {
public:
    PhysicsSystem physics;
    InputSystem   input;
    ScriptSystem  script;
    Scene         scene;

    World();

    void initialize();
};
