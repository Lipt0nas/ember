#pragma once

#include "ember.hpp"

class InputSystem {
public:
    void update_key_states();

    glm::vec2 get_mouse_position();

    bool is_key_pressed(int key);
    bool is_button_pressed(int button);

    bool is_key_just_pressed(int key);
    bool is_button_just_pressed(int button);

    glm::vec2 mouse_pos;

    std::array<bool, 512> pressed_keys    = {0};
    std::array<bool, 12>  pressed_buttons = {0};

    std::array<bool, 512> released_keys    = {1};
    std::array<bool, 12>  released_buttons = {1};
};
