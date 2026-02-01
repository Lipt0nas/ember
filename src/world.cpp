#include "world.hpp"

World::World() {
}

void World::initialize() {
    this->scene.initialize(this);
    this->script.initialize(this);
}
